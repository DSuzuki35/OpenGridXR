'use strict';

// Constructors (and their require calls) run only when a feature has work to do.
// Generation checks also dispose asynchronous constructors that finish after disable.
function createModuleServices({ factories, isEnabled }) {
  const slots = new Map();
  let closed = false;
  function slotFor(id) {
    if (!Object.hasOwn(factories, id)) throw new Error('Unknown optional service.');
    if (!slots.has(id)) slots.set(id, { generation: 0 });
    return slots.get(id);
  }
  const api = {
    peek(id) { return slots.get(id)?.instance; },
    async get(id) {
      if (closed) throw new Error('Optional services are closed.');
      if (!isEnabled(id)) throw new Error(`The ${id} module is disabled in Settings.`);
      const slot = slotFor(id);
      if (slot.closing) await slot.closing;
      if (closed || !isEnabled(id)) throw new Error(`The ${id} module is disabled or stopped.`);
      if (slot.instance) return slot.instance;
      if (!slot.pending) {
        const generation = slot.generation;
        slot.pending = Promise.resolve().then(factories[id]).then(async instance => {
          if (closed || generation !== slot.generation || !isEnabled(id)) {
            await instance.close();
            throw new Error(`The ${id} module is disabled or stopped.`);
          }
          slot.instance = instance;
          return instance;
        }).finally(() => { slot.pending = null; });
      }
      return slot.pending;
    },
    close(id) {
      const slot = slots.get(id);
      if (!slot) return Promise.resolve();
      if (slot.closing) return slot.closing;
      slot.generation++;
      const instance = slot.instance, pending = slot.pending;
      slot.instance = undefined;
      slot.closing = (async () => {
        if (instance) await instance.close();
        else if (pending) await pending.catch(() => {});
      })().finally(() => { slot.closing = null; });
      return slot.closing;
    },
    async closeAll() {
      closed = true;
      // A recorder may still need telemetry while it finalizes its report.
      await api.close('benchmark');
      await Promise.all([...slots.keys()].filter(id => id !== 'benchmark').map(id => api.close(id)));
    },
  };
  return api;
}
module.exports = { createModuleServices };
