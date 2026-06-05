/**
 * WebSocket client for the dfu-remote daemon (CLNR binary protocol).
 *
 * A port of the Android RemoteClient, but the transport is a browser WebSocket
 * so the HTTPS flasher can reach a local/LAN daemon over plain ws:// - Chrome's
 * Local Network Access permission exempts ws:// to a local host from
 * mixed-content blocking, so no TLS/cert/proxy is needed.
 *
 * The daemon treats the WebSocket as a byte stream (it sends each protocol field
 * as its own binary frame), so this client buffers all inbound bytes and parses
 * complete CLNR response frames out of the stream.
 */

const MAGIC = 0x434c4e52; // "CLNR"
const VERSION = 1;
export const DEFAULT_PORT = 5050;

const CMD_DISCOVER = 0x01;
const CMD_BOOTSTRAP = 0x02;
const CMD_WRITE = 0x03;
const CMD_READ = 0x04;
const CMD_CLONER_FLAG = 0x80;

const RESP_OK = 0x00;
const RESP_ERROR = 0x01;
const RESP_PROGRESS = 0x02;
const RESP_LOG = 0x03;

const VARIANT_NAMES = ['t10', 't20', 't21', 't23', 't30', 't31', 't31x', 't31zx',
    't31a', 'a1', 't40', 't41', 't32', 'x1000', 'x1600', 'x1700', 'x2000',
    'x2100', 'x2600', 't31al', 't40xp', 't23dl'];

/* CRC-32 (IEEE, reflected) - matches the daemon's remote_crc32 / zlib crc32. */
function crc32(bytes) {
    let crc = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) {
        crc ^= bytes[i];
        for (let j = 0; j < 8; j++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
    return (~crc) >>> 0;
}

export class RemoteClient {
    constructor(onLog, onProgress) {
        this.ws = null;
        this.onLog = onLog || function () {};
        this.onProgress = onProgress || function () {};
        this.rx = new Uint8Array(0);
        this.waiters = [];
        this.useCloner = false;
        this.closed = false;
    }

    connect(url, token) {
        return new Promise((resolve, reject) => {
            let ws;
            try {
                ws = new WebSocket(url);
            } catch (e) {
                reject(e);
                return;
            }
            ws.binaryType = 'arraybuffer';
            this.ws = ws;
            ws.onmessage = (ev) => this._onData(new Uint8Array(ev.data));
            ws.onerror = () => {};
            ws.onclose = () => { this.closed = true; this._failWaiters('connection closed'); };
            ws.onopen = async () => {
                try {
                    if (token) {
                        const tb = new TextEncoder().encode(token);
                        const buf = new Uint8Array(6 + tb.length);
                        new DataView(buf.buffer).setUint32(0, MAGIC);
                        buf[4] = VERSION;
                        buf[5] = tb.length;
                        buf.set(tb, 6);
                        ws.send(buf);
                        const r = await this._readResponse();
                        if (r.status !== RESP_OK) { reject(new Error('authentication failed')); return; }
                    }
                    resolve(true);
                } catch (e) { reject(e); }
            };
        });
    }

    disconnect() {
        if (this.ws) { try { this.ws.close(); } catch (e) { /* ignore */ } }
        this.ws = null;
    }

    isConnected() { return !!this.ws && this.ws.readyState === WebSocket.OPEN; }

    _onData(chunk) {
        const merged = new Uint8Array(this.rx.length + chunk.length);
        merged.set(this.rx);
        merged.set(chunk, this.rx.length);
        this.rx = merged;
        this._service();
    }

    _service() {
        while (this.waiters.length && this.rx.length >= this.waiters[0].need) {
            const w = this.waiters.shift();
            const out = this.rx.slice(0, w.need);
            this.rx = this.rx.slice(w.need);
            w.resolve(out);
        }
    }

    _failWaiters(msg) {
        while (this.waiters.length) this.waiters.shift().reject(new Error(msg));
    }

    _readExact(n) {
        return new Promise((resolve, reject) => {
            this.waiters.push({ need: n, resolve, reject });
            this._service();
            if (this.closed && this.rx.length < n) this._failWaiters('connection closed');
        });
    }

    async _readResponse() {
        const hdr = await this._readExact(10);
        const dv = new DataView(hdr.buffer, hdr.byteOffset, 10);
        if (dv.getUint32(0) !== MAGIC) throw new Error('bad response magic');
        const status = hdr[5];
        const plen = dv.getUint32(6);
        const payload = plen > 0 ? await this._readExact(plen) : new Uint8Array(0);
        return { status, payload };
    }

    _send(command, payload) {
        const cmd = this.useCloner ? (command | CMD_CLONER_FLAG) : command;
        const pl = payload || new Uint8Array(0);
        const buf = new Uint8Array(10 + pl.length);
        const dv = new DataView(buf.buffer);
        dv.setUint32(0, MAGIC);
        buf[4] = VERSION;
        buf[5] = cmd;
        dv.setUint32(6, pl.length);
        buf.set(pl, 10);
        this.ws.send(buf);
    }

    /* Read responses, surfacing PROGRESS/LOG, until OK (returns payload) or
     * ERROR (returns null). */
    async _drain() {
        for (;;) {
            const { status, payload } = await this._readResponse();
            if (status === RESP_PROGRESS) {
                if (payload.length >= 4) {
                    const percent = payload[0];
                    const msgLen = (payload[2] << 8) | payload[3];
                    const msg = msgLen > 0 && payload.length >= 4 + msgLen
                        ? new TextDecoder().decode(payload.subarray(4, 4 + msgLen)) : '';
                    this.onProgress(percent, msg);
                }
            } else if (status === RESP_LOG) {
                this.onLog(new TextDecoder().decode(payload));
            } else if (status === RESP_OK) {
                return payload;
            } else {
                const m = payload.length ? new TextDecoder().decode(payload) : 'unknown error';
                this.onLog('ERROR: ' + m + '\n');
                return null;
            }
        }
    }

    async discover() {
        this._send(CMD_DISCOVER);
        const payload = await this._drain();
        if (!payload) return [];
        const dv = new DataView(payload.buffer, payload.byteOffset, payload.length);
        const devs = [];
        for (let off = 0; off + 8 <= payload.length; off += 8) {
            const variant = dv.getUint8(off + 7);
            const stage = dv.getUint8(off + 6);
            devs.push({
                bus: dv.getUint8(off), address: dv.getUint8(off + 1),
                vendor: dv.getUint16(off + 2), product: dv.getUint16(off + 4),
                stage, variant,
                variantName: VARIANT_NAMES[variant] || 'unknown',
                stageName: stage === 0 ? 'bootrom' : 'firmware',
            });
        }
        return devs;
    }

    _variantPayload(deviceIndex, variant) {
        const vb = new TextEncoder().encode(variant || '');
        const p = new Uint8Array(2 + vb.length);
        p[0] = deviceIndex & 0xff;
        p[1] = vb.length;
        p.set(vb, 2);
        return p;
    }

    async bootstrap(deviceIndex, variant) {
        this._send(CMD_BOOTSTRAP, this._variantPayload(deviceIndex, variant));
        return (await this._drain()) !== null;
    }

    async readFirmware(deviceIndex, variant) {
        this._send(CMD_READ, this._variantPayload(deviceIndex, variant));
        const resp = await this._drain();
        if (!resp || resp.length < 4) return null;
        const data = resp.subarray(0, resp.length - 4);
        const recvCrc = new DataView(resp.buffer, resp.byteOffset + resp.length - 4, 4).getUint32(0) >>> 0;
        if (crc32(data) !== recvCrc) { this.onLog('ERROR: CRC32 mismatch on read\n'); return null; }
        return data.slice();
    }

    async writeFirmware(deviceIndex, variant, firmwareData) {
        const vb = new TextEncoder().encode(variant || '');
        const buf = new Uint8Array(2 + vb.length + 4 + firmwareData.length + 4);
        const dv = new DataView(buf.buffer);
        buf[0] = deviceIndex & 0xff;
        buf[1] = vb.length;
        buf.set(vb, 2);
        let off = 2 + vb.length;
        dv.setUint32(off, firmwareData.length);
        off += 4;
        buf.set(firmwareData, off);
        off += firmwareData.length;
        dv.setUint32(off, crc32(firmwareData));
        this._send(CMD_WRITE, buf);
        return (await this._drain()) !== null;
    }
}
