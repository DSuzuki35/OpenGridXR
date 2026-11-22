const dgram = require('node:dgram');
const { performance } = require('node:perf_hooks');

// oVRlay's SimHub observer sends the final physical colors, independently per device.
const models = new Map(['hyper-p1', 'hyper-sl', 'gxl', 'gt-max', 'fpe2', 'ascher-artura'].map((name, i) => [name, i + 1]));
const counts = [0, 88, 86, 62, 76, 66, 63];
const staleMs = 1000;

function decodeFrame(packet) {
  if (packet.length < 16) return null;
  const signature = packet.toString('latin1', 0, 8);
  const legacy = signature === 'AGLED001';
  if (!legacy && (signature !== 'OVRLED02' || packet.length < 20)) return null;
  const model = legacy ? 1 : packet.readUInt32LE(16);
  const count = counts[model];
  const header = legacy ? 16 : 20;
  if (!count || count !== packet.readUInt32LE(12) || packet.length !== header + count * 3) return null;
  return { model, sequence: packet.readUInt32LE(8), legacy, rgb: packet.subarray(header) };
}

function createWheelLights({ port = 29543, openSocket = () => dgram.createSocket('udp4'), now = () => performance.now() } = {}) {
  const latest = new Map();
  let socket, retry, enabled = false, error = '';

  function disconnect() {
    clearTimeout(retry);
    latest.clear();
    if (socket) {
      const old = socket;
      socket = undefined;
      old.close();
    }
  }
  function connect() {
    const candidate = openSocket();
    socket = candidate;
    candidate.on('message', (packet, source) => {
      if (socket !== candidate || source.address !== '127.0.0.1') return;
      const frame = decodeFrame(packet);
      if (!frame) return;
      const time = now();
      const prev = latest.get(frame.model);
      if (prev && time - prev.time <= staleMs) {
        if (frame.legacy && !prev.legacy) return;
        const distance = (frame.sequence - prev.sequence) >>> 0;
        if (frame.legacy === prev.legacy && (distance === 0 || distance >= 0x80000000)) return;
      }
      latest.set(frame.model, { ...frame, time });
    });
    candidate.once('listening', () => { if (socket === candidate) error = ''; });
    candidate.on('error', failure => {
      if (candidate !== socket) return;
      error = failure.code === 'EADDRINUSE'
        ? 'Local LED port 29543 is busy. Close the other oVRlay or OpenGrid lights receiver.'
        : 'Cannot listen for SimHub on this PC. Retrying the local LED connection.';
      disconnect();
      if (enabled) retry = setTimeout(connect, 3000).unref();
    });
    candidate.bind(port, '127.0.0.1');
    candidate.unref();
  }
  function close() {
    enabled = false;
    error = '';
    disconnect();
  }
  return {
    get error() { return error; },
    setEnabled(value) {
      if (!value) return close();
      if (enabled) return;
      enabled = true;
      connect();
    },
    sample(kind) {
      const frame = latest.get(models.get(kind) ?? kind);
      return frame && now() - frame.time <= staleMs ? frame.rgb : null;
    },
    close,
  };
}

module.exports = { createWheelLights };
