const {
  app,
  BrowserWindow,
  ipcMain,
  dialog,
  systemPreferences,
  session,
} = require("electron");
const path = require("path");
const fs = require("fs");
const os = require("os");
const { spawn, execFile } = require("child_process");

// 🚀 EXTREME PERFORMANCE: Force Hardware GPU Acceleration, Zero-Copy & Unthrottled Threads
app.commandLine.appendSwitch("ignore-gpu-blocklist");
app.commandLine.appendSwitch("enable-gpu-rasterization");
app.commandLine.appendSwitch("enable-zero-copy");
app.commandLine.appendSwitch("disable-software-rasterizer");
app.commandLine.appendSwitch("enable-hardware-overlays");
app.commandLine.appendSwitch("disable-renderer-backgrounding");
app.commandLine.appendSwitch("disable-background-timer-throttling");
app.commandLine.appendSwitch("disable-backgrounding-occluded-windows");
app.commandLine.appendSwitch("autoplay-policy", "no-user-gesture-required");
app.commandLine.appendSwitch(
  "enable-features",
  "VaapiVideoDecoder,VaapiVideoEncoder,CanvasOopRasterization,WebRTCPipeWireCapturer",
);

let mainWindow = null;
let engineProcess = null;

// 🌐 UNIVERSAL CROSS-PLATFORM BINARY RESOLUTION (Windows, macOS, Linux)
const getEngineBinaryPath = () => {
  const rootBuildDir = path.join(__dirname, "../../build");

  if (process.platform === "win32") {
    const ReleaseExe = path.join(rootBuildDir, "Release/AIStudioEngine.exe");
    const StandardExe = path.join(rootBuildDir, "AIStudioEngine.exe");
    return fs.existsSync(ReleaseExe) ? ReleaseExe : StandardExe;
  }

  if (process.platform === "darwin") {
    // Check direct C++ binary first (prevents macOS Dock icon while keeping bundled dylibs)
    const bundledBin = path.join(
      rootBuildDir,
      "AIStudioEngine.app/Contents/MacOS/AIStudioEngine_bin",
    );
    const directBin = path.join(rootBuildDir, "AIStudioEngine");
    if (fs.existsSync(bundledBin)) return bundledBin;
    if (fs.existsSync(directBin)) return directBin;
    return path.join(
      rootBuildDir,
      "AIStudioEngine.app/Contents/MacOS/AIStudioEngine",
    );
  }

  // Linux (Standard ELF binary)
  return path.join(rootBuildDir, "AIStudioEngine");
};

// 🔌 Cross-Platform ADB Resolver for Direct USB Type-C Phone Camera Tunneling
const resolveAdbBinary = () => {
  const home = os.homedir();
  const candidates =
    process.platform === "win32"
      ? [
          "adb.exe",
          path.join(home, "AppData/Local/Android/Sdk/platform-tools/adb.exe"),
          "C:\\Program Files\\DroidCam\\adb.exe",
        ]
      : [
          "/opt/homebrew/bin/adb",
          "/usr/local/bin/adb",
          "/usr/bin/adb",
          path.join(home, "Library/Android/sdk/platform-tools/adb"),
          path.join(home, "Android/Sdk/platform-tools/adb"),
        ];

  for (const candidate of candidates) {
    if (candidate.includes(path.sep) && fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return process.platform === "win32" ? "adb.exe" : "adb";
};

// Silently forwards USB Type-C ports (8080 IP Webcam, 4747 DroidCam, 8554 RTSP)
function setupUsbTypeCBridge() {
  return new Promise((resolve) => {
    const adbBin = resolveAdbBinary();
    const ports = [8080, 4747, 8554];

    execFile(adbBin, ["devices"], { timeout: 1500 }, (err, stdout) => {
      if (err) {
        return resolve({
          ok: false,
          devices: [],
          message:
            "ADB not found or no USB Type-C debugging device active (Direct HTTP/RTSP stream still supported).",
        });
      }

      const lines = (stdout || "")
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter((l) => l && !l.startsWith("List of devices"));

      let completed = 0;
      ports.forEach((p) => {
        execFile(
          adbBin,
          ["forward", `tcp:${p}`, `tcp:${p}`],
          { timeout: 1000 },
          () => {
            completed++;
            if (completed === ports.length) {
              resolve({
                ok: lines.length > 0,
                devices: lines,
                ports,
                message:
                  lines.length > 0
                    ? `USB Type-C device bridged on ports ${ports.join(", ")}`
                    : "No USB Type-C Android device listed in ADB yet",
              });
            }
          },
        );
      });
    });
  });
}

async function requestMediaPermissions() {
  if (process.platform === "darwin") {
    try {
      await systemPreferences.askForMediaAccess("camera");
      await systemPreferences.askForMediaAccess("microphone");
    } catch (err) {
      console.warn("[Electron] Media permission warning:", err);
    }
  }
}

function startEngine() {
  const enginePath = getEngineBinaryPath();
  const projectRoot = path.join(__dirname, "../../");

  console.log(
    `[Electron] Spawning headless AI Engine (${process.platform}): ${enginePath}`,
  );

  try {
    // Spawn directly as a headless child process (NO extra taskbar/dock icon on any OS)
    engineProcess = spawn(enginePath, [], {
      cwd: projectRoot,
      stdio: "inherit",
      windowsHide: true,
      env: {
        ...process.env,
        OPENCV_VIDEOIO_PRIORITY_MSMF: "0", // Faster USB camera init on Windows
        OPENCV_FFMPEG_CAPTURE_OPTIONS:
          "fflags;nobuffer|flags;low_delay|rtsp_transport;tcp", // Zero-lag USB/RTSP stream decoding
      },
    });

    engineProcess.on("error", (err) => {
      console.error("[Electron] Engine process error:", err);
    });

    engineProcess.on("exit", (code, signal) => {
      console.log(
        `[Electron] AI Engine exited (code: ${code}, signal: ${signal})`,
      );
      engineProcess = null;
    });
  } catch (err) {
    console.error("[Electron] Engine launch failed:", err);
  }
}

function stopEngine() {
  if (engineProcess && !engineProcess.killed) {
    try {
      if (process.platform === "win32") {
        spawn("taskkill", ["/pid", engineProcess.pid, "/f", "/t"]);
      } else {
        engineProcess.kill("SIGTERM");
      }
    } catch (e) {
      console.warn("[Electron] Error stopping engine:", e);
    }
    engineProcess = null;
  }
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1380,
    height: 880,
    minWidth: 1100,
    minHeight: 700,
    backgroundColor: "#020617",
    title: "AI Studio DeepLive Suite",
    show: false,
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      webSecurity: false,
      backgroundThrottling: false,
      spellcheck: false,
    },
  });

  mainWindow.once("ready-to-show", () => {
    mainWindow.show();
  });

  if (process.env.NODE_ENV === "development") {
    mainWindow.loadURL("http://localhost:5173");
  } else {
    mainWindow.loadFile(path.join(__dirname, "../dist/index.html"));
  }
}

ipcMain.handle("dialog:openFile", async (event, options) => {
  const { canceled, filePaths } = await dialog.showOpenDialog(mainWindow, {
    properties: ["openFile"],
    filters: options?.filters || [{ name: "All Files", extensions: ["*"] }],
  });
  return canceled || filePaths.length === 0 ? null : filePaths[0];
});

ipcMain.handle("usb:bridgeTypeC", async () => {
  return await setupUsbTypeCBridge();
});

app.whenReady().then(async () => {
  // Always grant WebRTC / MediaDevice enumeration for internal, USB, and Camo cameras
  session.defaultSession.setPermissionRequestHandler(
    (webContents, permission, callback) => {
      if (permission === "media" || permission === "mediaKeySystem") {
        return callback(true);
      }
      callback(false);
    },
  );

  session.defaultSession.setPermissionCheckHandler(
    (webContents, permission) => {
      if (permission === "media" || permission === "mediaKeySystem") {
        return true;
      }
      return false;
    },
  );

  await requestMediaPermissions();
  await setupUsbTypeCBridge();
  startEngine();
  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  stopEngine();
  if (process.platform !== "darwin") app.quit();
});

app.on("before-quit", () => {
  stopEngine();
});
