/**
 * HTTP client for the dfu-remote daemon (CLNR binary protocol over fetch()).
 *
 * Each command is one POST whose body is the CLNR command frame; the daemon
 * replies with a chunked stream of CLNR response frames (progress/log then
 * OK/ERROR), which we parse as a byte stream. fetch() is used (not WebSocket)
 * because Chrome's Local Network Access only exempts fetch({targetAddressSpace:
 * 'local'}) from mixed-content blocking - so the HTTPS flasher can reach a
 * local/LAN daemon over plain http:// with a one-time permission, no TLS.
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

/* Accept ws://, wss://, http://, https:// or a bare host[:port]; the transport
 * is plain HTTP (LNA can't exempt ws://). */
function normalizeUrl(url) {
    url = (url || '').trim();
    if (url.startsWith('ws://')) return 'http://' + url.slice(5);
    if (url.startsWith('wss://')) return 'https://' + url.slice(6);
    if (!/^https?:\/\//.test(url)) return 'http://' + url;
    return url;
}

export class RemoteClient {
    constructor(onLog, onProgress) {
        this.url = '';
        this.token = '';
        this.onLog = onLog || function () {};
        this.onProgress = onProgress || function () {};
        this.useCloner = false;
        this.connected = false;
    }

    async connect(url, token) {
        this.url = normalizeUrl(url);
        this.token = token || '';
        this.connected = true; // fetch() is stateless; the first command validates connectivity.
        return true;
    }

    disconnect() { this.connected = false; }
    isConnected() { return this.connected; }

    /* POST a CLNR command and parse the streamed CLNR responses, surfacing
     * PROGRESS/LOG, returning the OK payload (or null on ERROR). */
    async _command(command, payload) {
        const cmd = this.useCloner ? (command | CMD_CLONER_FLAG) : command;
        const pl = payload || new Uint8Array(0);
        const frame = new Uint8Array(10 + pl.length);
        const dv = new DataView(frame.buffer);
        dv.setUint32(0, MAGIC);
        frame[4] = VERSION;
        frame[5] = cmd;
        dv.setUint32(6, pl.length);
        frame.set(pl, 10);

        const headers = { 'Content-Type': 'application/octet-stream' };
        if (this.token) headers['X-Auth-Token'] = this.token;

        const resp = await fetch(this.url, {
            method: 'POST',
            headers,
            body: frame,
            // Tell Chrome this targets the local network so LNA exempts it from
            // mixed-content blocking (ignored by browsers without LNA).
            targetAddressSpace: 'local',
        });
        if (!resp.ok) throw new Error('HTTP ' + resp.status + ' ' + resp.statusText);
        if (!resp.body) throw new Error('no response body');

        const reader = resp.body.getReader();
        const queue = [];
        let queued = 0;
        let streamDone = false;
        const pump = async () => {
            const { done, value } = await reader.read();
            if (done) { streamDone = true; return false; }
            queue.push(value);
            queued += value.length;
            return true;
        };
        const readExact = async (n) => {
            while (queued < n) {
                if (!(await pump())) throw new Error('stream ended early');
            }
            const out = new Uint8Array(n);
            let o = 0;
            while (o < n) {
                const head = queue[0];
                const take = Math.min(head.length, n - o);
                out.set(head.subarray(0, take), o);
                o += take;
                queued -= take;
                if (take === head.length) queue.shift();
                else queue[0] = head.subarray(take);
            }
            return out;
        };

        for (;;) {
            const hdr = await readExact(10);
            const hv = new DataView(hdr.buffer, hdr.byteOffset, 10);
            if (hv.getUint32(0) !== MAGIC) throw new Error('bad response magic');
            const status = hdr[5];
            const plen = hv.getUint32(6);
            const body = plen > 0 ? await readExact(plen) : new Uint8Array(0);
            if (status === RESP_PROGRESS) {
                if (body.length >= 4) {
                    const percent = body[0];
                    const msgLen = (body[2] << 8) | body[3];
                    const msg = msgLen > 0 && body.length >= 4 + msgLen
                        ? new TextDecoder().decode(body.subarray(4, 4 + msgLen)) : '';
                    this.onProgress(percent, msg);
                }
            } else if (status === RESP_LOG) {
                this.onLog(new TextDecoder().decode(body));
            } else if (status === RESP_OK) {
                try { reader.cancel(); } catch (e) { /* ignore */ }
                return body;
            } else {
                const m = body.length ? new TextDecoder().decode(body) : 'unknown error';
                this.onLog('ERROR: ' + m + '\n');
                try { reader.cancel(); } catch (e) { /* ignore */ }
                return null;
            }
        }
    }

    async discover() {
        const payload = await this._command(CMD_DISCOVER);
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
        return (await this._command(CMD_BOOTSTRAP, this._variantPayload(deviceIndex, variant))) !== null;
    }

    async readFirmware(deviceIndex, variant) {
        const resp = await this._command(CMD_READ, this._variantPayload(deviceIndex, variant));
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
        return (await this._command(CMD_WRITE, buf)) !== null;
    }
}
