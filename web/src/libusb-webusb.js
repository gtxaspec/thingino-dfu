/**
 * libusb → WebUSB shim for Emscripten
 *
 * Uses Asyncify.handleAsync() to properly pause/resume WASM
 * when calling async WebUSB APIs.
 */

mergeInto(LibraryManager.library, {

    $webusb_state: {
        devices: [],
        handles: [],
        device_list: null,
        device_descriptors: [],
        next_handle_id: 1,
        handle_device_map: new Map(),
        // Diagnostics always go to the DevTools console; they reach the in-page
        // log (via Emscripten's err()->printErr, rendered by app.js) only when
        // the page was loaded with ?debug (app.js sets window.__tdfu_debug).
        log: function(msg) {
            try { console.log(msg); } catch (e) {}
            try {
                if (typeof window !== 'undefined' && window.__tdfu_debug && typeof err === 'function')
                    err(msg);
            } catch (e) {}
        },
        // Build a human-readable dump of a WebUSB device's descriptor tree.
        dump: function(d) {
            var s = (d.vendorId || 0).toString(16) + ':' + (d.productId || 0).toString(16) +
                ' opened=' + !!d.opened + ' activeCfg=' + (d.configuration ? (d.configuration.configurationValue) : 'none') +
                ' nCfg=' + (d.configurations ? d.configurations.length : 0);
            var cfgs = d.configurations || [];
            for (var c = 0; c < cfgs.length; c++) {
                var ifs = cfgs[c].interfaces || [];
                s += ' | cfg#' + cfgs[c].configurationValue + '[';
                for (var ii = 0; ii < ifs.length; ii++) {
                    var alts = ifs[ii].alternates || [];
                    for (var jj = 0; jj < alts.length; jj++) {
                        var a = alts[jj];
                        s += 'if' + ifs[ii].interfaceNumber + '.alt' + a.alternateSetting +
                            '=cls' + a.interfaceClass + '/sub' + a.interfaceSubclass + '/proto' + a.interfaceProtocol +
                            (a.interfaceName ? '("' + a.interfaceName + '")' : '') + ' ';
                    }
                }
                s += ']';
            }
            return s;
        },
    },

    $webusb_state__deps: [],
    $webusb_state__postset: '',

    /* ------------------------------------------------------------------ */
    /*  Init / Exit                                                        */
    /* ------------------------------------------------------------------ */

    libusb_init__deps: ['$webusb_state'],
    libusb_init: function(ctx_ptr) {
        if (ctx_ptr) {{{ makeSetValue('ctx_ptr', '0', '0', 'i32') }}};
        return 0;
    },

    libusb_exit__deps: ['$webusb_state'],
    libusb_exit: function(ctx) {
        webusb_state.devices = [];
        webusb_state.handles = [];
        webusb_state.handle_device_map.clear();
    },

    /* ------------------------------------------------------------------ */
    /*  Device enumeration                                                 */
    /* ------------------------------------------------------------------ */

    libusb_get_device_list__deps: ['$webusb_state', 'malloc'],
    libusb_get_device_list__async: true,
    libusb_get_device_list: function(ctx, list_ptr) {
        return Asyncify.handleAsync(function() {
            var INGENIC_VIDS = [0x601A, 0xA108];
            var INGENIC_PIDS = [0x4770, 0xC309, 0x601A, 0x8887, 0x601E, 0x4D44];

            var tryFind = function(attempt, maxAttempts) {
                return navigator.usb.getDevices().then(function(allDevices) {
                    // Merge with devices from requestDevice()
                    var extra = (typeof window !== 'undefined' && window._webusb_devices) || [];
                    for (var i = 0; i < extra.length; i++) {
                        if (allDevices.indexOf(extra[i]) === -1) allDevices.push(extra[i]);
                    }
                    var devices = allDevices.filter(function(d) {
                        return INGENIC_VIDS.indexOf(d.vendorId) !== -1 &&
                               INGENIC_PIDS.indexOf(d.productId) !== -1;
                    });
                    if (devices.length > 0 || webusb_state.devices.length === 0 || attempt >= maxAttempts) {
                        return devices;
                    }
                    // Re-enumeration retry
                    return new Promise(function(r) { setTimeout(r, 500); }).then(function() {
                        return tryFind(attempt + 1, maxAttempts);
                    });
                });
            };

            return tryFind(0, 16).then(function(devices) {
                webusb_state.devices = devices;
                webusb_state.log('[shim] get_device_list: ' + devices.length + ' device(s)');
                for (var di = 0; di < devices.length; di++) {
                    webusb_state.log('[shim]   dev[' + di + '] ' + webusb_state.dump(devices[di]));
                }
                webusb_state.device_descriptors = [];
                for (var i = 0; i < devices.length; i++) {
                    webusb_state.device_descriptors.push({
                        idVendor: devices[i].vendorId,
                        idProduct: devices[i].productId,
                        bNumConfigurations: devices[i].configurations ? devices[i].configurations.length : 1,
                    });
                }

                var count = devices.length;
                var arr = _malloc((count + 1) * 4);
                if (!arr) return -11;

                for (var i = 0; i < count; i++) {
                    {{{ makeSetValue('arr', 'i * 4', 'i + 1', 'i32') }}};
                }
                {{{ makeSetValue('arr', 'count * 4', '0', 'i32') }}};
                {{{ makeSetValue('list_ptr', '0', 'arr', 'i32') }}};

                webusb_state.device_list = arr;
                return count;
            });
        });
    },

    libusb_free_device_list__deps: ['$webusb_state', 'free'],
    libusb_free_device_list: function(list, unref_devices) {
        if (list) _free(list);
        if (list === webusb_state.device_list) webusb_state.device_list = null;
    },

    /* ------------------------------------------------------------------ */
    /*  Device descriptor / addressing                                     */
    /* ------------------------------------------------------------------ */

    libusb_get_device_descriptor__deps: ['$webusb_state'],
    libusb_get_device_descriptor: function(dev_ptr, desc_ptr) {
        var idx = dev_ptr - 1;
        if (idx < 0 || idx >= webusb_state.device_descriptors.length) return -5;
        var d = webusb_state.device_descriptors[idx];
        for (var i = 0; i < 18; i++) {{{ makeSetValue('desc_ptr', 'i', '0', 'i8') }}};
        {{{ makeSetValue('desc_ptr', '8', 'd.idVendor', 'i16') }}};
        {{{ makeSetValue('desc_ptr', '10', 'd.idProduct', 'i16') }}};
        {{{ makeSetValue('desc_ptr', '17', 'd.bNumConfigurations', 'i8') }}};
        return 0;
    },

    libusb_get_bus_number__deps: [],
    libusb_get_bus_number: function(dev_ptr) { return 1; },

    libusb_get_device_address__deps: [],
    libusb_get_device_address: function(dev_ptr) { return dev_ptr; },

    /* ------------------------------------------------------------------ */
    /*  Open / Close / Ref                                                 */
    /* ------------------------------------------------------------------ */

    libusb_open__deps: ['$webusb_state'],
    libusb_open__async: true,
    libusb_open: function(dev_ptr, handle_ptr) {
        var idx = dev_ptr - 1;
        if (idx < 0 || idx >= webusb_state.devices.length) return -5;
        var device = webusb_state.devices[idx];

        return Asyncify.handleAsync(function() {
            var p = device.opened ? Promise.resolve() : device.open();
            return p.then(function() {
                var handle_id = webusb_state.next_handle_id++;
                webusb_state.handles[handle_id] = device;
                webusb_state.handle_device_map.set(handle_id, idx);
                {{{ makeSetValue('handle_ptr', '0', 'handle_id', 'i32') }}};
                return 0;
            }).catch(function(e) {
                webusb_state.log('[shim] libusb_open ' + (e && e.name) + ': ' + (e && e.message));
                if (e && e.name === 'SecurityError') {
                    webusb_state.log('[shim] The browser/OS denied USB access to this device. On Linux add a ' +
                        'udev rule granting access to this VID:PID, then replug and reconnect.');
                    return -3; // LIBUSB_ERROR_ACCESS
                }
                if (e && e.name === 'NotFoundError') return -4; // LIBUSB_ERROR_NO_DEVICE
                return -3;
            });
        });
    },

    libusb_close__deps: ['$webusb_state'],
    libusb_close: function(handle_ptr) {
        // Don't actually close the WebUSB device — keep it open for reuse.
        // Closing and reopening races in the browser. The device stays open
        // until the page is unloaded.
        var device = webusb_state.handles[handle_ptr];
        if (!device) return;
        delete webusb_state.handles[handle_ptr];
        webusb_state.handle_device_map.delete(handle_ptr);
    },

    libusb_ref_device__deps: [],
    libusb_ref_device: function(dev_ptr) { return dev_ptr; },

    libusb_unref_device__deps: [],
    libusb_unref_device: function(dev_ptr) {},

    /* ------------------------------------------------------------------ */
    /*  Configuration / Interface                                          */
    /* ------------------------------------------------------------------ */

    libusb_set_configuration__deps: ['$webusb_state'],
    libusb_set_configuration__async: true,
    libusb_set_configuration: function(handle_ptr, configuration) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        return Asyncify.handleAsync(function() {
            return device.selectConfiguration(configuration).then(function() {
                return 0;
            }).catch(function() { return 0; });
        });
    },

    libusb_get_configuration__deps: ['$webusb_state'],
    libusb_get_configuration: function(handle_ptr, config_ptr) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        // Report 0 (unconfigured) when WebUSB has no configuration selected yet,
        // so callers know to call set_configuration (selectConfiguration) before
        // claiming - WebUSB requires it.
        var v = device.configuration ? device.configuration.configurationValue : 0;
        {{{ makeSetValue('config_ptr', '0', 'v', 'i32') }}};
        return 0;
    },

    libusb_claim_interface__deps: ['$webusb_state'],
    libusb_claim_interface__async: true,
    libusb_claim_interface: function(handle_ptr, iface) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        return Asyncify.handleAsync(function() {
            return device.claimInterface(iface).then(function() { return 0; })
                .catch(function(e) { console.error('claimInterface:', e); return -6; });
        });
    },

    libusb_release_interface__deps: ['$webusb_state'],
    libusb_release_interface__async: true,
    libusb_release_interface: function(handle_ptr, iface) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        return Asyncify.handleAsync(function() {
            return device.releaseInterface(iface).then(function() { return 0; })
                .catch(function() { return 0; });
        });
    },

    libusb_kernel_driver_active__deps: [],
    libusb_kernel_driver_active: function() { return 0; },

    libusb_detach_kernel_driver__deps: [],
    libusb_detach_kernel_driver: function() { return 0; },

    /* ------------------------------------------------------------------ */
    /*  Transfers                                                          */
    /* ------------------------------------------------------------------ */

    libusb_control_transfer__deps: ['$webusb_state'],
    libusb_control_transfer__async: true,
    libusb_control_transfer: function(handle_ptr, bmRequestType, bRequest,
                                      wValue, wIndex, data_ptr, wLength, timeout) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;

        var isIn = (bmRequestType & 0x80) !== 0;
        // Decode bmRequestType: bits 6-5 = type, bits 4-0 = recipient. The
        // cloner uses vendor/device; DFU uses class/interface; GET_DESCRIPTOR /
        // SET_INTERFACE use standard.
        var typeBits = (bmRequestType >> 5) & 0x3;
        var recipBits = bmRequestType & 0x1f;
        var setup = {
            requestType: typeBits === 1 ? 'class' : typeBits === 2 ? 'vendor' : 'standard',
            recipient: recipBits === 1 ? 'interface' : recipBits === 2 ? 'endpoint'
                                                                       : recipBits === 3 ? 'other' : 'device',
            request: bRequest, value: wValue, index: wIndex };
        var timeoutMs = (timeout && timeout > 0) ? timeout : 5000;

        return Asyncify.handleAsync(function() {
            // WebUSB blocks standard control requests; emulate the ones the DFU
            // host needs from WebUSB's parsed config + high-level methods.
            if (typeBits === 0) {
                if (bRequest === 0x06 && isIn) { // GET_DESCRIPTOR
                    var descType = (wValue >> 8) & 0xff;
                    var bytes = null;
                    var cfg = device.configuration || (device.configurations && device.configurations[0]);
                    if (descType === 0x02 && cfg) { // CONFIGURATION
                        var ifs = cfg.interfaces || [];
                        var body = [];
                        var sawDfu = false;
                        for (var ii = 0; ii < ifs.length; ii++) {
                            var alts = ifs[ii].alternates || [];
                            for (var jj = 0; jj < alts.length; jj++) {
                                var al = alts[jj];
                                body.push(9, 4, ifs[ii].interfaceNumber & 0xff, al.alternateSetting & 0xff, 0,
                                          al.interfaceClass & 0xff, al.interfaceSubclass & 0xff,
                                          al.interfaceProtocol & 0xff, 0);
                                if (al.interfaceClass === 0xFE && al.interfaceSubclass === 0x01) sawDfu = true;
                            }
                        }
                        // WebUSB strips the DFU functional descriptor (0x21), so the host can't
                        // read wTransferSize/bcdDFU. Reconstruct one with the Ingenic U-Boot
                        // values (wTransferSize=4096, bcdDFU=1.10) so uploads use 4096-byte blocks.
                        if (sawDfu) {
                            body.push(0x09, 0x21, 0x0F, 0xFF, 0x00, 0x00, 0x10, 0x10, 0x01);
                        }
                        var totalLen = 9 + body.length;
                        bytes = new Uint8Array([9, 2, totalLen & 0xff, (totalLen >> 8) & 0xff, ifs.length & 0xff,
                                                (cfg.configurationValue || 1) & 0xff, 0, 0x80, 0].concat(body));
                    } else if (descType === 0x03) { // STRING (WebUSB hides indices)
                        bytes = new Uint8Array([2, 3]);
                    }
                    webusb_state.log('[shim] GET_DESCRIPTOR type=0x' + descType.toString(16) + ' wLen=' + wLength +
                        ' hasCfg=' + !!cfg + ' built=' + (bytes ? bytes.length : 'null') + ' ifclasses=' +
                        (cfg ? (cfg.interfaces || []).map(function(x) {
                            return (x.alternates || []).map(function(a) {
                                return a.interfaceClass + '/' + a.interfaceSubclass; }).join(','); }).join(';') : '(none)'));
                    if (!bytes) return Promise.resolve(-9);
                    var m = Math.min(bytes.length, wLength);
                    for (var k = 0; k < m; k++) {
                        {{{ makeSetValue('data_ptr', 'k', 'bytes[k]', 'i8') }}};
                    }
                    return Promise.resolve(m);
                }
                if (bRequest === 0x0B && !isIn) { // SET_INTERFACE (value=alt, index=iface)
                    return device.selectAlternateInterface(wIndex, wValue)
                        .then(function() { return 0; })
                        .catch(function(e) { console.error('selectAlternateInterface:', e); return -9; });
                }
                if (bRequest === 0x09 && !isIn) { // SET_CONFIGURATION
                    return device.selectConfiguration(wValue).then(function() { return 0; }).catch(function() { return 0; });
                }
                return Promise.resolve(-12);
            }

            var transferPromise;
            if (isIn) {
                transferPromise = device.controlTransferIn(setup, wLength).then(function(result) {
                    if (result.status !== 'ok') return -9;
                    var received = new Uint8Array(result.data.buffer);
                    for (var i = 0; i < received.length && i < wLength; i++) {
                        {{{ makeSetValue('data_ptr', 'i', 'received[i]', 'i8') }}};
                    }
                    return received.length;
                });
            } else {
                var sendData = new Uint8Array(0);
                if (wLength > 0 && data_ptr) {
                    sendData = new Uint8Array(wLength);
                    for (var i = 0; i < wLength; i++) {
                        sendData[i] = {{{ makeGetValue('data_ptr', 'i', 'i8') }}} & 0xFF;
                    }
                }
                transferPromise = device.controlTransferOut(setup, sendData).then(function(result) {
                    if (result.status !== 'ok') return -9;
                    return result.bytesWritten;
                });
            }
            // Race against timeout
            var timeoutPromise = new Promise(function(_, reject) {
                setTimeout(function() { reject({name: 'TimeoutError'}); }, timeoutMs);
            });
            return Promise.race([transferPromise, timeoutPromise]).catch(function(e) {
                if (e.name === 'TimeoutError') {
                    webusb_state.log('[shim] control_transfer TIMEOUT: type=' + setup.requestType + '/' + setup.recipient +
                        ' req=0x' + bRequest.toString(16) + ' val=0x' + wValue.toString(16) + ' idx=0x' + wIndex.toString(16) +
                        ' len=' + wLength + ' dir=' + (isIn ? 'IN' : 'OUT') + ' after ' + timeoutMs + 'ms');
                    return -7;
                }
                if (e.name === 'NotFoundError') return -4;
                if (e.name === 'NetworkError') { webusb_state.log('[shim] control_transfer NetworkError: req=0x' + bRequest.toString(16)); return -9; }
                webusb_state.log('[shim] control_transfer error: req=0x' + bRequest.toString(16) + ' ' + e.name + ': ' + e.message);
                return -1;
            });
        });
    },

    libusb_bulk_transfer__deps: ['$webusb_state'],
    libusb_bulk_transfer__async: true,
    libusb_bulk_transfer: function(handle_ptr, endpoint, data_ptr, length,
                                    transferred_ptr, timeout) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;

        var isIn = (endpoint & 0x80) !== 0;
        var epNum = endpoint & 0x0F;
        var timeoutMs = (timeout && timeout > 0) ? timeout : 30000;

        return Asyncify.handleAsync(function() {
            var transferPromise;
            if (isIn) {
                transferPromise = device.transferIn(epNum, length).then(function(result) {
                    if (result.status !== 'ok') {
                        if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', '0', 'i32') }}};
                        return -9;
                    }
                    var received = new Uint8Array(result.data.buffer);
                    var count = Math.min(received.length, length);
                    HEAPU8.set(received.subarray(0, count), data_ptr);
                    if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', 'count', 'i32') }}};
                    return 0;
                });
            } else {
                var sendData = HEAPU8.slice(data_ptr, data_ptr + length);
                transferPromise = device.transferOut(epNum, sendData).then(function(result) {
                    if (result.status !== 'ok') {
                        if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', '0', 'i32') }}};
                        return -9;
                    }
                    if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', 'result.bytesWritten', 'i32') }}};
                    return 0;
                });
            }
            // Race against timeout
            var timeoutPromise = new Promise(function(_, reject) {
                setTimeout(function() { reject({name: 'TimeoutError'}); }, timeoutMs);
            });
            return Promise.race([transferPromise, timeoutPromise]).catch(function(e) {
                if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', '0', 'i32') }}};
                if (e.name === 'TimeoutError') {
                    console.warn('bulk_transfer TIMEOUT: ep=0x' + endpoint.toString(16) +
                        ' len=' + length + ' dir=' + (isIn ? 'IN' : 'OUT') + ' after ' + timeoutMs + 'ms');
                    return -7;
                }
                if (e.name === 'NotFoundError') return -4;
                console.error('bulk_transfer error: ep=0x' + endpoint.toString(16) + ' ' + e.name + ': ' + e.message);
                return -1;
            });
        });
    },

    libusb_interrupt_transfer__deps: ['$webusb_state'],
    libusb_interrupt_transfer__async: true,
    libusb_interrupt_transfer: function(handle_ptr, endpoint, data_ptr, length,
                                        transferred_ptr, timeout) {
        // WebUSB doesn't distinguish interrupt from bulk
        return _libusb_bulk_transfer(handle_ptr, endpoint, data_ptr, length,
                                     transferred_ptr, timeout);
    },

    /* ------------------------------------------------------------------ */
    /*  Reset / Error names                                                */
    /* ------------------------------------------------------------------ */

    libusb_reset_device__deps: ['$webusb_state'],
    libusb_reset_device__async: true,
    libusb_reset_device: function(handle_ptr) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        return Asyncify.handleAsync(function() {
            return device.reset().then(function() { return 0; })
                .catch(function() { return 0; });
        });
    },

    libusb_error_name__deps: ['malloc'],
    libusb_error_name: function(errcode) {
        var names = {
            0: "LIBUSB_SUCCESS", '-1': "LIBUSB_ERROR_IO",
            '-2': "LIBUSB_ERROR_INVALID_PARAM", '-3': "LIBUSB_ERROR_ACCESS",
            '-4': "LIBUSB_ERROR_NO_DEVICE", '-5': "LIBUSB_ERROR_NOT_FOUND",
            '-6': "LIBUSB_ERROR_BUSY", '-7': "LIBUSB_ERROR_TIMEOUT",
            '-8': "LIBUSB_ERROR_OVERFLOW", '-9': "LIBUSB_ERROR_PIPE",
            '-10': "LIBUSB_ERROR_INTERRUPTED", '-11': "LIBUSB_ERROR_NO_MEM",
            '-12': "LIBUSB_ERROR_NOT_SUPPORTED", '-99': "LIBUSB_ERROR_OTHER",
        };
        var name = names[String(errcode)] || "LIBUSB_UNKNOWN_ERROR";
        if (!_libusb_error_name._cache) _libusb_error_name._cache = {};
        if (!_libusb_error_name._cache[errcode]) {
            var len = name.length + 1;
            var ptr = _malloc(len);
            stringToUTF8(name, ptr, len);
            _libusb_error_name._cache[errcode] = ptr;
        }
        return _libusb_error_name._cache[errcode];
    },
});
