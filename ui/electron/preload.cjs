const { contextBridge, ipcRenderer } = require("electron");

console.log(
  "[DEBUG][ui/electron/preload.cjs] Initializing Electron contextBridge API...",
);

contextBridge.exposeInMainWorld("electronAPI", {
  openFileDialog: (options) => {
    console.log(
      "[DEBUG][ui/electron/preload.cjs::openFileDialog] Invoking dialog:openFile",
    );
    return ipcRenderer.invoke("dialog:openFile", options);
  },
  bridgeUsbTypeC: (autoLaunch = true) => {
    console.log(
      `[DEBUG][ui/electron/preload.cjs::bridgeUsbTypeC] Invoking usb:bridgeTypeC (autoLaunch=${autoLaunch})`,
    );
    return ipcRenderer.invoke("usb:bridgeTypeC", autoLaunch);
  },
  getPhoneBridgeStatus: () => ipcRenderer.invoke("usb:getPhoneBridgeStatus"),
  platform: process.platform,
});
