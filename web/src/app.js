/**
 * Thingino Web Flasher — Application Logic
 *
 * Loads the WASM module, drives the cloner C API from JS,
 * and manages the UI state machine.
 */

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */

var Module = null;
var clonerReady = false;
var currentState = 'idle';
var firmwareData = null;
var firmwareFileName = '';
var detectedVariant = -1;
var detectedVariantName = '';
var wasmBusy = false;

/**
 * Serialize all WASM async ccalls — Asyncify only supports one at a time.
 */
async function wasmCall(name, returnType, argTypes, args) {
    while (wasmBusy) {
        await new Promise(function(r) { setTimeout(r, 50); });
    }
    wasmBusy = true;
    try {
        return await Module.ccall(name, returnType, argTypes, args, {async: true});
    } finally {
        wasmBusy = false;
    }
}

/* Variant→firmware directory mapping (mirrors loader.c variant_to_firmware_dir) */
var VARIANT_DIR_MAP = {
    'a1': 'a1_n_ne_x',
    't31zx': 't31x',
    't31al': 't31x',
    't23dl': 't23',
    't40xp': 't40',
};

function variantToFirmwareDir(name) {
    return VARIANT_DIR_MAP[name] || name;
}

/* ------------------------------------------------------------------ */
/*  Logging                                                            */
/* ------------------------------------------------------------------ */

function log(msg, level) {
    level = level || 'info';
    var el = document.getElementById('log');
    var line = document.createElement('div');
    line.className = level;
    line.textContent = msg;
    el.appendChild(line);
    el.scrollTop = el.scrollHeight;
}

/* ------------------------------------------------------------------ */
/*  Progress                                                           */
/* ------------------------------------------------------------------ */

function showProgress(percent, label) {
    var container = document.getElementById('progress');
    container.classList.remove('d-none');
    document.getElementById('progress-fill').style.width = percent + '%';
    document.getElementById('progress-label').textContent = label || '';
}

function hideProgress() {
    document.getElementById('progress').classList.add('d-none');
}

/* ------------------------------------------------------------------ */
/*  UI State                                                           */
/* ------------------------------------------------------------------ */

function setState(state) {
    currentState = state;
    var badge = document.getElementById('status-badge');

    var labels = {
        idle: ['Idle', 'secondary'],
        connecting: ['Connecting...', 'warning'],
        detecting: ['Detecting...', 'warning'],
        bootstrapping: ['Bootstrapping...', 'warning'],
        writing: ['Writing...', 'warning'],
        reading: ['Reading...', 'warning'],
        done: ['Ready', 'success'],
        error: ['Error', 'danger'],
    };
    var info = labels[state] || ['Unknown', 'secondary'];
    badge.textContent = info[0];
    badge.className = 'badge bg-' + info[1] + ' ms-auto';

    var busy = ['connecting', 'detecting', 'bootstrapping', 'writing', 'reading'].indexOf(state) !== -1;
    var warn = document.getElementById('op-warning');
    if (['bootstrapping', 'writing', 'reading'].indexOf(state) !== -1) {
        warn.classList.remove('d-none');
    } else {
        warn.classList.add('d-none');
    }
    document.getElementById('btn-connect').disabled = busy;
    document.getElementById('btn-bootstrap').disabled = busy || state === 'idle';
    document.getElementById('btn-write').disabled = busy || state === 'idle';
    document.getElementById('btn-read').disabled = busy || state === 'idle';
}

function showDeviceInfo(soc, stage, vid, pid) {
    document.getElementById('device-disconnected').classList.add('d-none');
    document.getElementById('device-info').classList.remove('d-none');
    document.getElementById('info-soc').textContent = soc || '—';
    document.getElementById('info-stage').textContent = stage || '—';
    document.getElementById('info-vidpid').textContent =
        '0x' + vid.toString(16).padStart(4, '0') + ':0x' + pid.toString(16).padStart(4, '0');
}

/* ------------------------------------------------------------------ */
/*  MEMFS Firmware Loading                                             */
/* ------------------------------------------------------------------ */

/**
 * Fetch a firmware binary from the web server and write it into
 * Emscripten's virtual filesystem so the C code can fopen() it.
 */
async function loadFirmwareFileToMemFS(variant) {
    var dir = variantToFirmwareDir(variant);
    var basePath = './firmwares/' + dir;

    // Create directories in MEMFS
    try { Module.FS.mkdir('./firmwares'); } catch (e) { /* exists */ }
    try { Module.FS.mkdir(basePath); } catch (e) { /* exists */ }

    var files = ['spl.bin', 'uboot.bin'];
    for (var i = 0; i < files.length; i++) {
        var url = 'firmware/' + dir + '/' + files[i];
        var memPath = basePath + '/' + files[i];

        console.log('Fetching ' + url + '...');
        var response = await fetch(url);
        if (!response.ok) {
            throw new Error('Failed to fetch ' + url + ': ' + response.status);
        }
        var data = new Uint8Array(await response.arrayBuffer());
        Module.FS.writeFile(memPath, data);
        console.log('Loaded ' + files[i] + ': ' + data.length + ' bytes');
    }

    // ddr.bin not fetched — always generated dynamically by the C DDR system
}

/* ------------------------------------------------------------------ */
/*  Device discovery helper                                            */
/* ------------------------------------------------------------------ */

/**
 * Call cloner_discover_devices and parse the first device's info.
 * Returns { bus, addr, vid, pid, stage, variant, variantName } or null.
 */
async function discoverDevices() {
    var listPtr = Module._malloc(8);
    console.log('ccall cloner_discover_devices, listPtr=' + listPtr);
    var result = await wasmCall('cloner_discover_devices', 'number', ['number'], [listPtr]);
    console.log('cloner_discover_devices returned:', result);

    if (result !== 0) {
        console.log('discover FAILED with error', result);
        Module._free(listPtr);
        return null;
    }

    var devicesArrayPtr = Module.HEAPU32[listPtr >> 2];
    var count = Module.HEAPU32[(listPtr + 4) >> 2];
    console.log('devicesArrayPtr=' + devicesArrayPtr + ' count=' + count);

    if (count === 0) {
        Module.ccall('cloner_free_device_list', null, ['number'], [listPtr]);
        Module._free(listPtr);
        return null;
    }

    // cloner_device_info_t layout (16 bytes, packed):
    //   u8 bus, u8 address, u16 vendor_id, u16 product_id, 2 pad,
    //   i32 stage, i32 variant
    var base = devicesArrayPtr;
    var bus = Module.HEAPU8[base];
    var addr = Module.HEAPU8[base + 1];
    var vid = Module.HEAPU8[base + 2] | (Module.HEAPU8[base + 3] << 8);
    var pid = Module.HEAPU8[base + 4] | (Module.HEAPU8[base + 5] << 8);
    var stage = Module.HEAPU32[(base + 8) >> 2];
    var variant = Module.HEAPU32[(base + 12) >> 2];

    var namePtr = Module.ccall('cloner_variant_to_string', 'number', ['number'], [variant]);
    var variantName = namePtr ? Module.UTF8ToString(namePtr) : 'unknown';

    Module.ccall('cloner_free_device_list', null, ['number'], [listPtr]);
    Module._free(listPtr);

    return {
        bus: bus, addr: addr, vid: vid, pid: pid,
        stage: stage, variant: variant, variantName: variantName
    };
}

/* ------------------------------------------------------------------ */
/*  WASM Module Init                                                   */
/* ------------------------------------------------------------------ */

async function initModule() {
    console.log('Loading WASM module...');

    try {
        Module = await createClonerModule({
            printErr: function(text) {
                if (text.startsWith('[DEBUG]')) {
                    log(text, 'debug');
                } else if (text.startsWith('[WARN]')) {
                    log(text, 'warn');
                } else if (text.startsWith('[ERROR]')) {
                    log(text, 'error');
                } else {
                    log(text, 'info');
                }
            },
            print: function(text) {
                log(text, 'info');
            },
        });

        console.log('WASM module loaded (' +
            (Module.HEAPU8.length / 1024 / 1024).toFixed(1) + ' MB heap)');

        // {async:true} is required — cloner_init calls libusb_init which is async
        var result = await wasmCall('cloner_init', 'number', [], []);
        if (result !== 0) {
            log('cloner_init failed: ' + result, 'error');
            return;
        }

        // Create /tmp for the C code's temp file operations
        try { Module.FS.mkdir('/tmp'); } catch (e) { /* exists */ }

        clonerReady = true;

        // Display version
        var verPtr = Module.ccall('cloner_get_version', 'number', [], []);
        var version = verPtr ? Module.UTF8ToString(verPtr) : 'dev';
        var verEl = document.getElementById('version-text');
        if (verEl) verEl.textContent = 'thingino-cloner v' + version;

        log('Ready — click Connect Device to begin');
    } catch (e) {
        log('Failed to initialize — check console for details', 'error');
        console.error(e);
    }
}

/* ------------------------------------------------------------------ */
/*  Connect Device                                                     */
/* ------------------------------------------------------------------ */

async function connectDevice() {
    if (!clonerReady) {
        log('Module not ready', 'warn');
        return;
    }

    setState('connecting');
    log('Requesting USB device...');

    try {
        // Call WebUSB requestDevice directly — request permission for ALL
        // Ingenic VID:PID pairs (bootrom + firmware) so re-enumeration works
        var filters = [
            { vendorId: 0x601A, productId: 0x4770 },
            { vendorId: 0x601A, productId: 0xC309 },
            { vendorId: 0xA108, productId: 0xC309 },
            { vendorId: 0x601A, productId: 0x8887 },
            { vendorId: 0xA108, productId: 0x8887 },
            { vendorId: 0x601A, productId: 0x601E },
            { vendorId: 0xA108, productId: 0x601E },
            { vendorId: 0x601A, productId: 0x601A },
            { vendorId: 0xA108, productId: 0x601A },
        ];

        var device;
        try {
            device = await navigator.usb.requestDevice({ filters: filters });
        } catch (e) {
            log('No device selected', 'warn');
            setState('idle');
            return;
        }

        // Store on global so the libusb shim can find it in getDevices()
        if (!window._webusb_devices) window._webusb_devices = [];
        var already = window._webusb_devices.some(function(d) { return d === device; });
        if (!already) window._webusb_devices.push(device);

        console.log('Device selected: VID=0x' + device.vendorId.toString(16) +
            ' PID=0x' + device.productId.toString(16));

        setState('detecting');
        console.log('Discovering devices...');

        var info = await discoverDevices();
        if (!info) {
            log('No Ingenic devices found', 'warn');
            setState('idle');
            return;
        }

        detectedVariant = info.variant;
        detectedVariantName = info.variantName;

        var stageName = info.stage === 0 ? 'Bootrom' : 'Firmware';
        showDeviceInfo(info.variantName.toUpperCase(), stageName, info.vid, info.pid);
        log('Detected: ' + info.variantName.toUpperCase() + ' (' + stageName + ')');

        setState('done');
    } catch (e) {
        log('Connection error: ' + e.message, 'error');
        console.error(e);
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Bootstrap                                                          */
/* ------------------------------------------------------------------ */

async function doBootstrap() {
    if (!clonerReady || detectedVariant < 0) return;

    setState('bootstrapping');
    showProgress(10, 'Loading firmware files...');
    log('Starting bootstrap for ' + detectedVariantName.toUpperCase() + '...');

    try {
        // Load firmware into MEMFS
        await loadFirmwareFileToMemFS(detectedVariantName);
        showProgress(30, 'Bootstrapping device...');

        // Call cloner_bootstrap(device_index=0, variant, firmware_dir=NULL, progress=NULL, user_data=NULL)
        // firmware_dir NULL means default "./firmwares"
        var result = await wasmCall('cloner_bootstrap', 'number',
            ['number', 'number', 'number', 'number', 'number'],
            [0, detectedVariant, 0, 0, 0]);

        if (result !== 0) {
            log('Bootstrap failed: error ' + result, 'error');
            hideProgress();
            setState('error');
            return;
        }

        showProgress(80, 'Re-discovering device...');
        console.log('Bootstrap complete, re-discovering device...');

        // Re-discover to update device state (now in firmware stage)
        var info = await discoverDevices();
        if (info) {
            // Restore the original variant — re-enumeration defaults to T31X
            Module.ccall('cloner_set_device_variant', 'number',
                ['number', 'number'], [0, detectedVariant]);
            info.variant = detectedVariant;
            info.variantName = detectedVariantName;

            var stageName = info.stage === 0 ? 'Bootrom' : 'Firmware';
            showDeviceInfo(detectedVariantName.toUpperCase(), stageName, info.vid, info.pid);
            log('Device now in ' + stageName + ' stage');
        }

        showProgress(100, 'Bootstrap complete');
        log('Bootstrap completed successfully');
        setTimeout(hideProgress, 1500);
        setState('done');
    } catch (e) {
        log('Bootstrap error: ' + e.message, 'error');
        console.error(e);
        hideProgress();
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Write Firmware                                                     */
/* ------------------------------------------------------------------ */

function selectFirmware() {
    document.getElementById('firmware-file').click();
}

function firmwareSelected(input) {
    if (!input.files || !input.files[0]) return;

    var file = input.files[0];
    firmwareFileName = file.name;

    var reader = new FileReader();
    reader.onload = function(e) {
        firmwareData = new Uint8Array(e.target.result);
        var sizeMB = (firmwareData.length / (1024 * 1024)).toFixed(2);
        document.getElementById('file-info').textContent =
            firmwareFileName + ' (' + sizeMB + ' MB)';
        log('Firmware loaded: ' + firmwareFileName + ' (' + sizeMB + ' MB)');

        // Auto-start write if device is in firmware stage
        doWrite();
    };
    reader.readAsArrayBuffer(file);
}

async function doWrite() {
    if (!clonerReady || !firmwareData) return;

    // Always bootstrap from JS if device is in bootrom — the internal
    // bootstrap in cloner_op_write_firmware doesn't handle WebUSB re-enumeration
    var info = await discoverDevices();
    if (!info) {
        log('No device found', 'error');
        setState('error');
        return;
    }
    if (info.stage === 0) {
        log('Device in bootrom stage — bootstrapping first...');
        await doBootstrap();

        info = await discoverDevices();
        if (!info || info.stage === 0) {
            log('Bootstrap failed — device still in bootrom', 'error');
            setState('error');
            return;
        }
    }

    setState('writing');
    showProgress(10, 'Preparing firmware write...');

    // Preload bootstrap firmware to MEMFS in case ops layer needs it
    await loadFirmwareFileToMemFS(detectedVariantName);

    log('Writing ' + firmwareFileName + ' (' + firmwareData.length + ' bytes)...');

    try {
        // Allocate WASM memory for firmware data
        var dataPtr = Module._malloc(firmwareData.length);
        if (!dataPtr) {
            log('Failed to allocate WASM memory', 'error');
            setState('error');
            return;
        }
        Module.HEAPU8.set(firmwareData, dataPtr);

        showProgress(30, 'Writing to flash...');

        // cloner_write_firmware(device_index=0, firmware, len, progress=NULL, user_data=NULL)
        var result = await wasmCall('cloner_write_firmware', 'number',
            ['number', 'number', 'number', 'number', 'number'],
            [0, dataPtr, firmwareData.length, 0, 0]);

        Module._free(dataPtr);

        if (result !== 0) {
            log('Write failed: error ' + result, 'error');
            hideProgress();
            setState('error');
            return;
        }

        showProgress(100, 'Write complete');
        log('Firmware written successfully!');
        setTimeout(hideProgress, 1500);
        setState('done');
    } catch (e) {
        log('Write error: ' + e.message, 'error');
        console.error(e);
        hideProgress();
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Read Firmware                                                      */
/* ------------------------------------------------------------------ */

async function doRead() {
    if (!clonerReady) return;

    // Always bootstrap from JS if device is in bootrom
    var info = await discoverDevices();
    if (!info) {
        log('No device found', 'error');
        setState('error');
        return;
    }
    if (info.stage === 0) {
        log('Device in bootrom stage — bootstrapping first...');
        await doBootstrap();
    }

    setState('reading');
    showProgress(10, 'Preparing firmware read...');
    log('Reading firmware from flash...');

    try {
        // Re-discover to ensure fresh device state
        var readInfo = await discoverDevices();
        if (!readInfo) {
            log('No device found for read', 'error');
            hideProgress();
            setState('error');
            return;
        }
        // Set correct variant after re-discovery
        Module.ccall('cloner_set_device_variant', 'number',
            ['number', 'number'], [0, detectedVariant]);
        console.log('Read target: ' + detectedVariantName + ' (' + (readInfo.stage === 0 ? 'Bootrom' : 'Firmware') + ')');

        // Preload firmware to MEMFS — cloner_op_read_firmware may bootstrap internally
        await loadFirmwareFileToMemFS(detectedVariantName);

        // Allocate output pointers
        var fwPtrPtr = Module._malloc(4);  // uint8_t**
        var lenPtr = Module._malloc(4);    // size_t*
        Module.HEAPU32[fwPtrPtr >> 2] = 0;
        Module.HEAPU32[lenPtr >> 2] = 0;

        showProgress(30, 'Reading from flash...');
        console.log('cloner_read_firmware args: idx=0 fwPtrPtr=' + fwPtrPtr + ' lenPtr=' + lenPtr);

        // cloner_read_firmware(device_index=0, &firmware, &len, progress=NULL, user_data=NULL)
        var result = await wasmCall('cloner_read_firmware', 'number',
            ['number', 'number', 'number', 'number', 'number'],
            [0, fwPtrPtr, lenPtr, 0, 0]);
        console.log('cloner_read_firmware returned:', result);

        if (result !== 0) {
            log('Read failed: error ' + result, 'error');
            Module._free(fwPtrPtr);
            Module._free(lenPtr);
            hideProgress();
            setState('error');
            return;
        }

        var dataPtr = Module.HEAPU32[fwPtrPtr >> 2];
        var dataLen = Module.HEAPU32[lenPtr >> 2];

        log('Read ' + dataLen + ' bytes from flash');

        // Copy from WASM heap and trigger download
        var data = Module.HEAPU8.slice(dataPtr, dataPtr + dataLen);
        Module._free(dataPtr);
        Module._free(fwPtrPtr);
        Module._free(lenPtr);

        // Create download
        var blob = new Blob([data], { type: 'application/octet-stream' });
        var url = URL.createObjectURL(blob);
        var a = document.createElement('a');
        a.href = url;
        a.download = 'firmware_dump.bin';
        a.click();
        URL.revokeObjectURL(url);

        showProgress(100, 'Read complete');
        log('Firmware saved as firmware_dump.bin');
        setTimeout(hideProgress, 1500);
        setState('done');
    } catch (e) {
        log('Read error: ' + e.message, 'error');
        console.error(e);
        hideProgress();
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */

// Expose handlers referenced by HTML onclick/onchange attributes
Object.assign(window, { connectDevice, doBootstrap, selectFirmware, firmwareSelected, doRead });

(function() {
    if (!navigator.usb) {
        document.getElementById('browser-warning').classList.remove('d-none');
        document.querySelector('.flasher-card').classList.add('d-none');
        return;
    }

    navigator.usb.addEventListener('connect', function(e) {
        console.log('USB device connected: VID=0x' + e.device.vendorId.toString(16) +
            ' PID=0x' + e.device.productId.toString(16));
    });
    navigator.usb.addEventListener('disconnect', function(e) {
        console.log('USB device disconnected');
    });

    setState('idle');
    initModule();
})();
