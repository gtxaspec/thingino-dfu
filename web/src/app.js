/**
 * Thingino Web Flasher — Application Logic
 *
 * Loads the WASM module, drives the tdfu C API from JS,
 * and manages the UI state machine.
 */

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */

var Module = null;
var tdfuReady = false;
var currentState = 'idle';
var firmwareData = null;
var firmwareFileName = '';
var detectedVariant = -1;
var detectedVariantName = '';
var wasmBusy = false;

/* Optional user-supplied DFU bootloader (Advanced area). When BOTH are set, the
 * next DFU Bootstrap uses these instead of the bundled images and skips SoC
 * detection. Each is { name: string, data: Uint8Array } or null. */
var customSpl = null;
var customUboot = null;

/* Backend: 'dfu' (default) drives U-Boot DFU; 'cloner' is the legacy Ingenic
 * USB-boot flash protocol. Persisted across reloads. */
var backendMode = localStorage.getItem('tdfu_backend') || 'dfu';
var inDfuMode = false; /* connected device is a U-Boot DFU gadget (a108:4d44) */

/* Verbose [DEBUG]/[shim] diagnostics are gated behind ?debug in the URL. The
 * shim reads window.__tdfu_debug; the C side is toggled via tdfu_web_set_debug()
 * once the module loads. Normal info/warn/error always show. */
var debugEnabled = new URLSearchParams(location.search).has('debug') || /\bdebug\b/.test(location.hash);
window.__tdfu_debug = debugEnabled;

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
var TDFU_VARIANT_DIR_MAP = {
    'a1': 'a1_n_ne_x',
    't31zx': 't31x',
    't31al': 't31x',
    't23dl': 't23',
    't40xp': 't40',
};

function variantToFirmwareDir(name) {
    return TDFU_VARIANT_DIR_MAP[name] || name;
}

/* DFU firmware directory mapping (mirrors dfu_variant_dir in dfu.c) */
var TDFU_DFU_DIR_MAP = {
    't23dl': 't23', 't31x': 't31', 't31zx': 't31', 't31al': 't31',
    't31a': 't31_ddr3', 't40xp': 't40_ddr3',
};
function dfuVariantDir(name) {
    return TDFU_DFU_DIR_MAP[name] || name;
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

/* Hex SHA-256 of a byte buffer via Web Crypto (available in the secure context
 * the flasher runs in). */
async function sha256Hex(data) {
    var digest = await crypto.subtle.digest('SHA-256', data);
    var bytes = new Uint8Array(digest);
    var hex = '';
    for (var i = 0; i < bytes.length; i++) hex += bytes[i].toString(16).padStart(2, '0');
    return hex;
}

/* Log "SHA256: ..." for a just-saved buffer; quietly skip if unavailable. */
async function logSha256(data) {
    try {
        log('SHA256: ' + await sha256Hex(data));
    } catch (e) {
        console.warn('sha256 failed', e);
    }
}

/* Unique dump filename matching the Android app: firmware_<SOC>_<yyyyMMdd_HHmm>.bin */
function readFilename() {
    var soc = (detectedVariantName || '').toUpperCase() || 'DFU';
    var d = new Date();
    var p = function(n) { return String(n).padStart(2, '0'); };
    var ts = '' + d.getFullYear() + p(d.getMonth() + 1) + p(d.getDate()) + '_' + p(d.getHours()) + p(d.getMinutes());
    return 'firmware_' + soc + '_' + ts + '.bin';
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
    // Enable actions based on the actual workflow, not just "a device exists":
    //  - DFU backend: a bootrom must be BOOTSTRAPPED into the U-Boot DFU gadget
    //    before Read/Write work, so only Bootstrap is live with a bootrom, and
    //    only Read/Write are live once we're in DFU mode.
    //  - Cloner backend: Read/Write/Bootstrap all act on the connected device.
    var hasDevice = state === 'done';
    var dfu = backendMode === 'dfu';
    var canBoot = hasDevice && !busy && !inDfuMode;          // bootstrap a bootrom (pointless once in DFU)
    var canRW = hasDevice && !busy && (dfu ? inDfuMode : true);

    document.getElementById('btn-connect').disabled = busy;
    document.getElementById('btn-bootstrap').disabled = !canBoot;
    document.getElementById('btn-write').disabled = !canRW;
    document.getElementById('btn-read').disabled = !canRW;

    // Glow the single "next action" so it's obvious what to click. In DFU mode
    // with a bootrom attached, that's Bootstrap.
    setAttention('btn-bootstrap', canBoot && dfu);
}

/* Toggle the pulsing "click me" glow on a button. */
function setAttention(id, on) {
    var el = document.getElementById(id);
    if (el) el.classList.toggle('btn-attention', !!on);
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
    var basePath = './firmware/cloner/' + dir;

    // Create directories in MEMFS
    try { Module.FS.mkdir('./firmware'); } catch (e) { /* exists */ }
    try { Module.FS.mkdir('./firmware/cloner'); } catch (e) { /* exists */ }
    try { Module.FS.mkdir(basePath); } catch (e) { /* exists */ }

    var files = ['spl.bin', 'uboot.bin'];
    for (var i = 0; i < files.length; i++) {
        var url = 'firmware/cloner/' + dir + '/' + files[i];
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

/**
 * Fetch the DFU-capable SPL + U-Boot for a variant into MEMFS at
 * ./firmware/dfu/<dir>/ so tdfu_web_dfu_bootstrap can USB-boot them.
 */
async function loadDfuFirmwareToMemFS(variant) {
    var dir = dfuVariantDir(variant);
    var basePath = './firmware/dfu/' + dir;
    try { Module.FS.mkdir('./firmware'); } catch (e) { /* exists */ }
    try { Module.FS.mkdir('./firmware/dfu'); } catch (e) { /* exists */ }
    try { Module.FS.mkdir(basePath); } catch (e) { /* exists */ }

    var files = ['spl.bin', 'uboot.bin'];
    for (var i = 0; i < files.length; i++) {
        var url = 'firmware/dfu/' + dir + '/' + files[i];
        var response = await fetch(url);
        if (!response.ok) {
            throw new Error('Failed to fetch ' + url + ': ' + response.status);
        }
        var data = new Uint8Array(await response.arrayBuffer());
        Module.FS.writeFile(basePath + '/' + files[i], data);
        console.log('Loaded dfu ' + files[i] + ': ' + data.length + ' bytes');
    }
}

/* ------------------------------------------------------------------ */
/*  Device discovery helper                                            */
/* ------------------------------------------------------------------ */

/**
 * Call tdfu_discover_devices and parse the first device's info.
 * Returns { bus, addr, vid, pid, stage, variant, variantName } or null.
 */
async function discoverDevices() {
    var listPtr = Module._malloc(8);
    console.log('ccall tdfu_discover_devices, listPtr=' + listPtr);
    var result = await wasmCall('tdfu_discover_devices', 'number', ['number'], [listPtr]);
    console.log('tdfu_discover_devices returned:', result);

    if (result !== 0) {
        console.log('discover FAILED with error', result);
        Module._free(listPtr);
        return null;
    }

    var devicesArrayPtr = Module.HEAPU32[listPtr >> 2];
    var count = Module.HEAPU32[(listPtr + 4) >> 2];
    console.log('devicesArrayPtr=' + devicesArrayPtr + ' count=' + count);

    if (count === 0) {
        Module.ccall('tdfu_free_device_list', null, ['number'], [listPtr]);
        Module._free(listPtr);
        return null;
    }

    // tdfu_device_info_t layout (16 bytes, packed):
    //   u8 bus, u8 address, u16 vendor_id, u16 product_id, 2 pad,
    //   i32 stage, i32 variant
    var base = devicesArrayPtr;
    var bus = Module.HEAPU8[base];
    var addr = Module.HEAPU8[base + 1];
    var vid = Module.HEAPU8[base + 2] | (Module.HEAPU8[base + 3] << 8);
    var pid = Module.HEAPU8[base + 4] | (Module.HEAPU8[base + 5] << 8);
    var stage = Module.HEAPU32[(base + 8) >> 2];
    var variant = Module.HEAPU32[(base + 12) >> 2];

    var namePtr = Module.ccall('tdfu_variant_to_string', 'number', ['number'], [variant]);
    var variantName = namePtr ? Module.UTF8ToString(namePtr) : 'unknown';

    Module.ccall('tdfu_free_device_list', null, ['number'], [listPtr]);
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
        Module = await createTdfuModule({
            printErr: function(text) {
                if (text.startsWith('[DEBUG]') || text.startsWith('[shim]')) {
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

        // Enable verbose C [DEBUG] logging only when ?debug is set.
        if (debugEnabled) {
            Module.ccall('tdfu_web_set_debug', null, ['number'], [1]);
            log('Debug logging enabled (?debug)', 'debug');
        }

        // {async:true} is required — tdfu_init calls libusb_init which is async
        var result = await wasmCall('tdfu_init', 'number', [], []);
        if (result !== 0) {
            log('tdfu_init failed: ' + result, 'error');
            return;
        }

        // Create /tmp for the C code's temp file operations
        try { Module.FS.mkdir('/tmp'); } catch (e) { /* exists */ }

        tdfuReady = true;

        // Display version
        var verPtr = Module.ccall('tdfu_get_version', 'number', [], []);
        var version = verPtr ? Module.UTF8ToString(verPtr) : 'dev';
        var verEl = document.getElementById('version-num');
        if (verEl) verEl.textContent = 'v' + version;

        log('Ready — click Connect Device to begin');
        // Nothing on initial load: auto-attach happens ONLY via the USB 'connect'
        // event (re-enumeration / hotplug of an already-authorized device).
    } catch (e) {
        log('Failed to initialize — check console for details', 'error');
        console.error(e);
    }
}

/* ------------------------------------------------------------------ */
/*  Connect Device                                                     */
/* ------------------------------------------------------------------ */

async function connectDevice() {
    if (!tdfuReady) {
        log('Module not ready', 'warn');
        return;
    }

    setState('connecting');
    setAttention('btn-connect', false); // user is acting on the prompt
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
            { vendorId: 0x601A, productId: 0x4D44 },
            { vendorId: 0xA108, productId: 0x4D44 },
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

        // A U-Boot DFU gadget (a108:4d44) is ready to flash directly; it isn't
        // an Ingenic bootrom, so skip the cloner detect path.
        if (device.productId === 0x4D44) {
            inDfuMode = true;
            showDeviceInfo(detectedVariantName ? detectedVariantName.toUpperCase() : '—', 'U-Boot DFU',
                       device.vendorId, device.productId);
            log('Device in U-Boot DFU mode — ready (Write to flash, Read to dump).');
            setState('done');
            return;
        }
        inDfuMode = false;

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

/* Attach a device we ALREADY have permission for, with no chooser and no user
 * gesture. Driven by the 'connect' event and a getDevices() sweep at load.
 *
 * This is what removes the manual re-pick after a bootstrap: once the DFU
 * gadget (a108:4d44) has been authorized once, WebUSB persists that grant, so
 * when the device re-enumerates the 'connect' event fires and we wire it up
 * automatically. The very first authorization still needs one chooser click
 * (WebUSB won't surface a device that isn't present yet, and the PID changes
 * across re-enumeration). */
async function autoAttachDevice(device) {
    if (!tdfuReady || !device) { log('autoAttach: not ready', 'debug'); return; }
    if (device.vendorId !== 0x601A && device.vendorId !== 0xA108) {
        log('autoAttach: ignoring non-Ingenic 0x' + device.vendorId.toString(16), 'debug');
        return;
    }
    // Never hijack an operation in flight (read/write/bootstrap).
    if (currentState !== 'idle' && currentState !== 'done') {
        log('autoAttach: skipped, busy (' + currentState + ')', 'debug');
        return;
    }
    log('autoAttach: attaching 0x' + device.productId.toString(16) + ' (state ' + currentState + ')', 'debug');

    if (!window._webusb_devices) window._webusb_devices = [];
    if (!window._webusb_devices.some(function(d) { return d === device; }))
        window._webusb_devices.push(device);
    setAttention('btn-connect', false); // device is here; no manual re-pick needed

    if (device.productId === 0x4D44) {
        inDfuMode = true;
        showDeviceInfo(detectedVariantName ? detectedVariantName.toUpperCase() : '—', 'U-Boot DFU',
                       device.vendorId, device.productId);
        log('Device reconnected in U-Boot DFU mode — ready (no re-pick needed).');
        setState('done');
        return;
    }

    // Bootrom / firmware stage: probe and show what it is.
    inDfuMode = false;
    setState('detecting');
    var info = await discoverDevices();
    if (!info) { setState('idle'); return; }
    detectedVariant = info.variant;
    detectedVariantName = info.variantName;
    var stageName = info.stage === 0 ? 'Bootrom' : 'Firmware';
    showDeviceInfo(info.variantName.toUpperCase(), stageName, info.vid, info.pid);
    log('Detected: ' + info.variantName.toUpperCase() + ' (' + stageName + ')');
    setState('done');
}

/* ------------------------------------------------------------------ */
/*  Bootstrap                                                          */
/* ------------------------------------------------------------------ */

async function doBootstrap() {
    if (!tdfuReady) return;
    // DFU bootstrap handles its own variant/custom-file check (custom SPL+U-Boot
    // need no detected variant); the cloner path below requires a variant.
    if (backendMode === 'dfu') return doDfuBootstrap();
    if (detectedVariant < 0) return;

    setState('bootstrapping');
    showProgress(10, 'Loading firmware files...');
    log('Starting bootstrap for ' + detectedVariantName.toUpperCase() + '...');

    try {
        // Load firmware into MEMFS
        await loadFirmwareFileToMemFS(detectedVariantName);
        showProgress(30, 'Bootstrapping device...');

        // Call tdfu_bootstrap(device_index=0, variant, firmware_dir=NULL, progress=NULL, user_data=NULL)
        // firmware_dir NULL means default "./firmware"
        var result = await wasmCall('tdfu_bootstrap', 'number',
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
            Module.ccall('tdfu_set_device_variant', 'number',
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
    if (!tdfuReady || !firmwareData) return;
    if (backendMode === 'dfu') return doDfuWrite();

    // Always bootstrap from JS if device is in bootrom — the internal
    // bootstrap in tdfu_op_write_firmware doesn't handle WebUSB re-enumeration
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

        // tdfu_write_firmware(device_index=0, firmware, len, progress=NULL, user_data=NULL)
        var result = await wasmCall('tdfu_write_firmware', 'number',
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
    if (!tdfuReady) return;
    if (backendMode === 'dfu') return doDfuRead();

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
        Module.ccall('tdfu_set_device_variant', 'number',
            ['number', 'number'], [0, detectedVariant]);
        console.log('Read target: ' + detectedVariantName + ' (' + (readInfo.stage === 0 ? 'Bootrom' : 'Firmware') + ')');

        // Preload firmware to MEMFS — tdfu_op_read_firmware may bootstrap internally
        await loadFirmwareFileToMemFS(detectedVariantName);

        // Allocate output pointers
        var fwPtrPtr = Module._malloc(4);  // uint8_t**
        var lenPtr = Module._malloc(4);    // size_t*
        Module.HEAPU32[fwPtrPtr >> 2] = 0;
        Module.HEAPU32[lenPtr >> 2] = 0;

        showProgress(30, 'Reading from flash...');
        console.log('tdfu_read_firmware args: idx=0 fwPtrPtr=' + fwPtrPtr + ' lenPtr=' + lenPtr);

        // tdfu_read_firmware(device_index=0, &firmware, &len, progress=NULL, user_data=NULL)
        var result = await wasmCall('tdfu_read_firmware', 'number',
            ['number', 'number', 'number', 'number', 'number'],
            [0, fwPtrPtr, lenPtr, 0, 0]);
        console.log('tdfu_read_firmware returned:', result);

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
        var fname = readFilename();
        var a = document.createElement('a');
        a.href = url;
        a.download = fname;
        a.click();
        URL.revokeObjectURL(url);

        showProgress(100, 'Read complete');
        log('Firmware saved as ' + fname);
        await logSha256(data);
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
/*  Advanced: custom DFU bootloader (optional SPL + U-Boot upload)      */
/* ------------------------------------------------------------------ */

function toggleAdvanced(e) {
    if (e) e.preventDefault();
    var controls = document.getElementById('adv-controls');
    var chev = document.getElementById('adv-chevron');
    var hidden = controls.classList.toggle('d-none');
    if (chev) chev.className = hidden ? 'bi bi-chevron-right' : 'bi bi-chevron-down';
}

/* Flip a Select button between its idle (blue outline + file icon) and loaded
 * (solid green + check) look, so it's obvious which images are staged. */
function setSelectButtonLoaded(id, loaded) {
    var b = document.getElementById(id);
    if (!b) return;
    var icon = b.querySelector('i');
    if (loaded) {
        b.classList.remove('btn-outline-primary');
        b.classList.add('btn-success');
        if (icon) icon.className = 'bi bi-check-circle-fill me-1';
    } else {
        b.classList.remove('btn-success');
        b.classList.add('btn-outline-primary');
        if (icon) icon.className = 'bi bi-file-earmark-binary me-1';
    }
}

/* Read a chosen file into a { name, data } record and report it. */
function readCustomBootloaderFile(input, which) {
    if (!input.files || !input.files[0]) return;
    var file = input.files[0];
    var isSpl = which === 'spl';
    var infoId = isSpl ? 'custom-spl-info' : 'custom-uboot-info';
    var btnId = isSpl ? 'btn-sel-spl' : 'btn-sel-uboot';
    var label = isSpl ? 'SPL' : 'U-Boot';
    var reader = new FileReader();
    reader.onload = function(e) {
        var rec = { name: file.name, data: new Uint8Array(e.target.result) };
        if (isSpl) customSpl = rec; else customUboot = rec;
        document.getElementById(infoId).textContent =
            label + ': ' + file.name + ' (' + rec.data.length + ' bytes)';
        setSelectButtonLoaded(btnId, true);
        log('Custom ' + label + ' selected: ' + file.name + ' (' + rec.data.length + ' bytes)');
        if (customSpl && customUboot)
            log('Custom SPL + U-Boot ready — next Bootstrap will use them.', 'warn');
    };
    reader.readAsArrayBuffer(file);
}

function customSplSelected(input) { readCustomBootloaderFile(input, 'spl'); }
function customUbootSelected(input) { readCustomBootloaderFile(input, 'uboot'); }

function clearCustomBootloader() {
    customSpl = null;
    customUboot = null;
    document.getElementById('custom-spl-info').textContent = 'SPL: bundled';
    document.getElementById('custom-uboot-info').textContent = 'U-Boot: bundled';
    document.getElementById('custom-spl-file').value = '';
    document.getElementById('custom-uboot-file').value = '';
    setSelectButtonLoaded('btn-sel-spl', false);
    setSelectButtonLoaded('btn-sel-uboot', false);
    log('Custom bootloader cleared — using bundled DFU U-Boot.');
}

/* ------------------------------------------------------------------ */
/*  DFU backend flows (default)                                        */
/* ------------------------------------------------------------------ */

async function doDfuBootstrap() {
    var custom = !!(customSpl && customUboot);
    if (detectedVariant < 0 && !custom) { log('Connect a device in bootrom mode first', 'warn'); return; }
    setState('bootstrapping');
    showProgress(10, custom ? 'Loading custom U-Boot...' : 'Loading DFU U-Boot...');
    log(custom ? 'DFU bootstrap with custom SPL + U-Boot...'
               : 'DFU bootstrap for ' + detectedVariantName.toUpperCase() + '...');
    try {
        var rc;
        if (custom) {
            Module.FS.writeFile('/tmp/custom_spl.bin', customSpl.data);
            Module.FS.writeFile('/tmp/custom_uboot.bin', customUboot.data);
            showProgress(40, 'USB-booting custom U-Boot (DFU)...');
            rc = await wasmCall('tdfu_web_dfu_bootstrap_files', 'number',
                ['number', 'string', 'string'], [0, '/tmp/custom_spl.bin', '/tmp/custom_uboot.bin']);
            try { Module.FS.unlink('/tmp/custom_spl.bin'); } catch (e) { /* ignore */ }
            try { Module.FS.unlink('/tmp/custom_uboot.bin'); } catch (e) { /* ignore */ }
        } else {
            await loadDfuFirmwareToMemFS(detectedVariantName);
            showProgress(40, 'USB-booting U-Boot (DFU)...');
            rc = await wasmCall('tdfu_web_dfu_bootstrap', 'number',
                ['number', 'string', 'string'], [0, './firmware', detectedVariantName]);
        }
        if (rc !== 0) {
            log('DFU bootstrap failed: error ' + rc, 'error');
            hideProgress(); setState('error'); return;
        }
        showProgress(100, 'U-Boot DFU running');
        log('Device is re-enumerating as a U-Boot DFU gadget.');
        log('First time: click Connect and pick "USB download gadget" once. After that it reconnects automatically.', 'warn');
        setTimeout(hideProgress, 1500);
        document.getElementById('device-info').classList.add('d-none');
        document.getElementById('device-disconnected').classList.remove('d-none');
        setState('idle');
        // Auto-attach happens via the USB 'connect' event when the gadget
        // enumerates (only if it was authorized before). One-shot (NOT a poll):
        // if it hasn't auto-attached within a few seconds, the gadget isn't
        // authorized yet, so glow Connect to prompt the one-time manual pick.
        setTimeout(function() {
            if (currentState === 'idle' && !inDfuMode) setAttention('btn-connect', true);
        }, 3000);
    } catch (e) {
        log('DFU bootstrap error: ' + e.message, 'error');
        console.error(e); hideProgress(); setState('error');
    }
}

async function doDfuRead() {
    if (!inDfuMode) { log('Not a DFU device — bootstrap into DFU mode first', 'warn'); return; }
    setState('reading');
    showProgress(20, 'Reading flash via DFU...');
    log('DFU upload (reading flash)...');
    try {
        var rc = await wasmCall('tdfu_web_dfu_upload', 'number',
            ['number', 'string', 'string', 'number'], [0, '', '/tmp/dfu-rd.bin', 0]);
        if (rc !== 0) { log('DFU read failed: error ' + rc, 'error'); hideProgress(); setState('error'); return; }
        var data = Module.FS.readFile('/tmp/dfu-rd.bin');
        try { Module.FS.unlink('/tmp/dfu-rd.bin'); } catch (e) { /* ignore */ }
        log('Read ' + data.length + ' bytes from flash (DFU)');
        var blob = new Blob([data], { type: 'application/octet-stream' });
        var url = URL.createObjectURL(blob);
        var fname = readFilename();
        var a = document.createElement('a'); a.href = url; a.download = fname; a.click();
        URL.revokeObjectURL(url);
        showProgress(100, 'Read complete'); log('Firmware saved as ' + fname);
        await logSha256(data);
        setTimeout(hideProgress, 1500); setState('done');
    } catch (e) {
        log('DFU read error: ' + e.message, 'error'); console.error(e); hideProgress(); setState('error');
    }
}

async function doDfuWrite() {
    if (!firmwareData) { log('Select a firmware file first', 'warn'); return; }
    if (!inDfuMode) { log('Not a DFU device — bootstrap into DFU mode first', 'warn'); return; }
    setState('writing');
    showProgress(20, 'Writing flash via DFU...');
    log('DFU download: ' + firmwareFileName + ' (' + firmwareData.length + ' bytes)...');
    try {
        Module.FS.writeFile('/tmp/dfu-wr.bin', firmwareData);
        var rc = await wasmCall('tdfu_web_dfu_download', 'number',
            ['number', 'string', 'string'], [0, '', '/tmp/dfu-wr.bin']);
        try { Module.FS.unlink('/tmp/dfu-wr.bin'); } catch (e) { /* ignore */ }
        if (rc !== 0) { log('DFU write failed: error ' + rc, 'error'); hideProgress(); setState('error'); return; }
        showProgress(100, 'Write complete'); log('Firmware written via DFU!');
        setTimeout(hideProgress, 1500); setState('done');
    } catch (e) {
        log('DFU write error: ' + e.message, 'error'); console.error(e); hideProgress(); setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Settings (backend selection)                                       */
/* ------------------------------------------------------------------ */

function applyBackendMode(mode) {
    backendMode = (mode === 'cloner') ? 'cloner' : 'dfu';
    localStorage.setItem('tdfu_backend', backendMode);
    var ind = document.getElementById('mode-indicator');
    if (ind) ind.textContent = backendMode === 'cloner' ? 'Cloner' : 'DFU';
    // The custom SPL/U-Boot override is a DFU-bootstrap feature; hide it in cloner mode.
    var adv = document.getElementById('adv-wrap');
    if (adv) adv.classList.toggle('d-none', backendMode !== 'dfu');
    setState(currentState); // re-evaluate which action buttons are live for this backend
}

function openSettings() {
    var r = document.getElementById('setting-' + backendMode);
    if (r) r.checked = true;
    document.getElementById('settings-overlay').classList.remove('d-none');
}

function closeSettings() {
    document.getElementById('settings-overlay').classList.add('d-none');
}

function saveSettings() {
    var sel = document.querySelector('input[name="backend-mode"]:checked');
    applyBackendMode(sel ? sel.value : 'dfu');
    log('Backend: ' + (backendMode === 'cloner' ? 'Cloner (legacy)' : 'DFU'));
    closeSettings();
}

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */

// Expose handlers referenced by HTML onclick/onchange attributes
Object.assign(window, { connectDevice, doBootstrap, selectFirmware, firmwareSelected, doRead,
                        openSettings, closeSettings, saveSettings,
                        toggleAdvanced, customSplSelected, customUbootSelected, clearCustomBootloader });

(function() {
    if (!navigator.usb) {
        document.getElementById('browser-warning').classList.remove('d-none');
        document.querySelector('.flasher-card').classList.add('d-none');
        return;
    }

    navigator.usb.addEventListener('connect', function(e) {
        var id = '0x' + e.device.vendorId.toString(16) + ':0x' + e.device.productId.toString(16);
        console.log('USB device connected: ' + id);
        log('USB connect event: ' + id, 'debug');
        // Re-attach automatically if we already have permission for it.
        autoAttachDevice(e.device);
    });
    navigator.usb.addEventListener('disconnect', function(e) {
        console.log('USB device disconnected: PID=0x' + e.device.productId.toString(16));
        if (window._webusb_devices)
            window._webusb_devices = window._webusb_devices.filter(function(d) { return d !== e.device; });
    });

    applyBackendMode(backendMode);
    setState('idle');
    initModule();
})();
