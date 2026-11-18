'use strict';
const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('openGridMenu', Object.freeze({
  onState(callback) {
    if (typeof callback !== 'function') throw new TypeError('State callback required.');
    const listener = (_event, state) => callback(state);
    ipcRenderer.on('opengrid:quickMenuState', listener);
    return () => ipcRenderer.removeListener('opengrid:quickMenuState', listener);
  },
  runAction(actionId) {
    if (typeof actionId !== 'string' || actionId.length < 1 || actionId.length > 160) return Promise.reject(new Error('Invalid menu action.'));
    return ipcRenderer.invoke('opengrid:quickMenuAction', actionId);
  },
}));
