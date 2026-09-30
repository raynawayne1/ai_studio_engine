const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("electronAPI", {
  openFileDialog: (options) => ipcRenderer.invoke("dialog:openFile", options),
  bridgeUsbTypeC: () => ipcRenderer.invoke("usb:bridgeTypeC"),
  platform: process.platform,
});
