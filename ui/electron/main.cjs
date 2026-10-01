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
const http = require("http");
const https = require("https");
const { spawn, execFile, execSync } = require("child_process");

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
app.commandLine.appendSwitch("ignore-certificate-errors");
app.commandLine.appendSwitch(
  "enable-features",
  "VaapiVideoDecoder,VaapiVideoEncoder,CanvasOopRasterization,WebRTCPipeWireCapturer",
);

let mainWindow = null;
let engineProcess = null;
let phoneHttpServer = null;
let phoneHttpsServer = null;
let phoneStreamFrameCount = 0;
let phoneForwardErrorCount = 0;
let lastPhoneFrameTimestamp = 0;

const HTTP_BRIDGE_PORT = 8766;
const HTTPS_BRIDGE_PORT = 8767;
const CPP_ENGINE_PORT = 8765;

// Persistent Keep-Alive HTTP Agent for sub-millisecond frame forwarding to C++ Engine
const cppKeepAliveAgent = new http.Agent({
  keepAlive: true,
  maxSockets: 4,
  keepAliveMsecs: 5000,
});

// 🌐 UNIVERSAL CROSS-PLATFORM BINARY RESOLUTION (Windows, macOS, Linux)
const getEngineBinaryPath = () => {
  const rootBuildDir = path.join(__dirname, "../../build");

  if (process.platform === "win32") {
    const ReleaseExe = path.join(rootBuildDir, "Release/AIStudioEngine.exe");
    const StandardExe = path.join(rootBuildDir, "AIStudioEngine.exe");
    return fs.existsSync(ReleaseExe) ? ReleaseExe : StandardExe;
  }

  if (process.platform === "darwin") {
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

  return path.join(rootBuildDir, "AIStudioEngine");
};

const resolvePreloadPath = () => {
  const candidates = [
    path.join(__dirname, "preload.cjs"),
    path.join(__dirname, "preload.js"),
    path.join(__dirname, "../preload.js"),
    path.join(__dirname, "../preload.cjs"),
  ];
  for (const p of candidates) {
    if (fs.existsSync(p)) {
      console.log(
        `[DEBUG][ui/electron/main.cjs::resolvePreloadPath] Using preload: ${p}`,
      );
      return p;
    }
  }
  const fallback = path.join(__dirname, "preload.cjs");
  console.warn(
    `[WARN][ui/electron/main.cjs::resolvePreloadPath] Preload not found; defaulting to: ${fallback}`,
  );
  return fallback;
};

// Discover all local & USB-C tethering IPv4 addresses (Android RNDIS/NCM 192.168.42.x, iPhone 172.20.10.x, LAN)
const getSystemNetworkAddresses = () => {
  const interfaces = os.networkInterfaces();
  const addresses = [];
  for (const [ifaceName, ifaceList] of Object.entries(interfaces)) {
    if (!ifaceList) continue;
    for (const iface of ifaceList) {
      if (iface.family === "IPv4" && !iface.internal) {
        const isUsbTether =
          iface.address.startsWith("192.168.42.") ||
          iface.address.startsWith("192.168.43.") ||
          iface.address.startsWith("172.20.10.") ||
          ifaceName.toLowerCase().includes("usb") ||
          ifaceName.toLowerCase().includes("rndis");
        addresses.push({
          iface: ifaceName,
          address: iface.address,
          isUsbTether,
          httpUrl: `http://${iface.address}:${HTTP_BRIDGE_PORT}/camera`,
          httpsUrl: `https://${iface.address}:${HTTPS_BRIDGE_PORT}/camera`,
        });
      }
    }
  }
  addresses.sort((a, b) => Number(b.isUsbTether) - Number(a.isUsbTether));
  return addresses;
};

// Mobile HTML5 Zero-Install Camera Transmitter Page served directly to the phone over USB-C / Local socket
const getMobileCameraHtml = () => `<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no" />
  <title>AI Studio USB-C Phone Camera</title>
  <style>
    body { margin: 0; background: #020617; color: #f8fafc; font-family: system-ui, sans-serif; display: flex; flex-direction: column; height: 100vh; overflow: hidden; }
    header { padding: 12px 16px; background: #0f172a; border-bottom: 1px solid #1e293b; display: flex; justify-content: space-between; align-items: center; }
    .badge { background: #16a34a; color: #fff; padding: 4px 10px; border-radius: 6px; font-size: 12px; font-weight: 700; }
    video { flex: 1; width: 100%; object-fit: cover; background: #000; }
    footer { padding: 14px; background: #0f172a; border-top: 1px solid #1e293b; display: flex; gap: 10px; justify-content: center; }
    button { padding: 12px 18px; border-radius: 8px; border: none; font-weight: 700; font-size: 14px; cursor: pointer; background: #0284c7; color: #fff; }
  </style>
</head>
<body>
  <header>
    <div>
      <strong style="color:#38bdf8;font-size:15px;">AI Studio USB-C Camera Bridge</strong>
      <div id="status" style="font-size:11px;color:#94a3b8;">Initializing phone camera...</div>
    </div>
    <span class="badge" id="fpsBadge">LIVE</span>
  </header>
  <video id="v" autoplay playsinline muted></video>
  <canvas id="c" width="640" height="360" style="display:none;"></canvas>
  <footer>
    <button onclick="switchFacing()">🔄 Flip Front / Back Camera</button>
  </footer>
  <script>
    let facing = 'user';
    let stream = null;
    let busy = false;
    let sent = 0;
    const video = document.getElementById('v');
    const canvas = document.getElementById('c');
    const ctx = canvas.getContext('2d', { alpha: false });
    const statusEl = document.getElementById('status');
    const fpsBadge = document.getElementById('fpsBadge');

    async function startCam() {
      try {
        if (stream) stream.getTracks().forEach(t => t.stop());
        stream = await navigator.mediaDevices.getUserMedia({
          audio: false,
          video: { facingMode: facing, width: { ideal: 1280 }, height: { ideal: 720 }, frameRate: { ideal: 30 } }
        });
        video.srcObject = stream;
        statusEl.textContent = 'Streaming HD frames to AI Studio C++ Engine...';
      } catch (e) {
        statusEl.textContent = 'Camera error: ' + e.message;
      }
    }

    function switchFacing() {
      facing = (facing === 'user') ? 'environment' : 'user';
      startCam();
    }

    setInterval(() => {
      if (busy || video.readyState < 2) return;
      busy = true;
      ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
      canvas.toBlob(async (blob) => {
        if (!blob) { busy = false; return; }
        try {
          await fetch('/push', { method: 'POST', body: blob });
          sent++;
          fpsBadge.textContent = 'FRAMES: ' + sent;
        } catch (e) {}
        busy = false;
      }, 'image/jpeg', 0.82);
    }, 33);

    startCam();
  </script>
</body>
</html>`;

// Forward incoming JPEG buffer from phone directly into C++ EngineIPCServer (POST http://127.0.0.1:8765/frame/push)
function forwardFrameToCppEngine(jpegBuffer) {
  const req = http.request(
    {
      hostname: "127.0.0.1",
      port: CPP_ENGINE_PORT,
      path: "/frame/push",
      method: "POST",
      agent: cppKeepAliveAgent,
      headers: {
        "Content-Type": "image/jpeg",
        "Content-Length": jpegBuffer.length,
        Connection: "keep-alive",
      },
      timeout: 400,
    },
    (res) => {
      res.resume();
    },
  );
  req.on("error", (err) => {
    phoneForwardErrorCount++;
    if (phoneForwardErrorCount === 1 || phoneForwardErrorCount % 60 === 0) {
      console.error(
        `[ERROR][ui/electron/main.cjs::forwardFrameToCppEngine] Failed forwarding frame to C++ port ${CPP_ENGINE_PORT}: ${err.message}`,
      );
    }
  });
  req.write(jpegBuffer);
  req.end();
}

// Handle HTTP/HTTPS requests from the connected Phone
function handlePhoneBridgeRequest(req, res) {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");

  if (req.method === "OPTIONS") {
    res.writeHead(204);
    return res.end();
  }

  if (
    req.method === "GET" &&
    (req.url === "/" || req.url.startsWith("/camera"))
  ) {
    console.log(
      `[DEBUG][ui/electron/main.cjs::handlePhoneBridgeRequest] Phone opened camera transmitter page (${req.socket.remoteAddress})`,
    );
    const html = getMobileCameraHtml();
    res.writeHead(200, {
      "Content-Type": "text/html; charset=utf-8",
      "Content-Length": Buffer.byteLength(html),
    });
    return res.end(html);
  }

  if (req.method === "POST" && req.url.startsWith("/push")) {
    const chunks = [];
    req.on("data", (chunk) => chunks.push(chunk));
    req.on("end", () => {
      const buffer = Buffer.concat(chunks);
      if (buffer.length > 32) {
        phoneStreamFrameCount++;
        lastPhoneFrameTimestamp = Date.now();
        if (phoneStreamFrameCount === 1 || phoneStreamFrameCount % 120 === 0) {
          console.log(
            `[DEBUG][ui/electron/main.cjs::handlePhoneBridgeRequest] Phone pushed live JPEG frame #${phoneStreamFrameCount} (${buffer.length} bytes) -> forwarding to C++ Engine`,
          );
        }
        forwardFrameToCppEngine(buffer);
      }
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end('{"status":"ok"}');
    });
    return;
  }

  res.writeHead(200, { "Content-Type": "application/json" });
  res.end('{"status":"ok","service":"AIStudioPhoneBridge"}');
}

// Start built-in HTTP (:8766) & HTTPS (:8767) Phone Camera Ingestion Servers
function startBuiltInPhoneBridgeServers() {
  try {
    phoneHttpServer = http.createServer(handlePhoneBridgeRequest);
    phoneHttpServer.on("error", (err) => {
      console.warn(
        `[WARN][ui/electron/main.cjs::startBuiltInPhoneBridgeServers] HTTP :${HTTP_BRIDGE_PORT} notice:`,
        err.message,
      );
    });
    phoneHttpServer.listen(HTTP_BRIDGE_PORT, "0.0.0.0", () => {
      console.log(
        `[DEBUG][ui/electron/main.cjs::startBuiltInPhoneBridgeServers] USB-C Phone Bridge HTTP server active on port ${HTTP_BRIDGE_PORT}`,
      );
    });

    const keyPath = path.join(os.tmpdir(), "aistudio_phone_key.pem");
    const certPath = path.join(os.tmpdir(), "aistudio_phone_cert.pem");
    if (!fs.existsSync(keyPath) || !fs.existsSync(certPath)) {
      try {
        execSync(
          `openssl req -x509 -newkey rsa:2048 -keyout "${keyPath}" -out "${certPath}" -days 365 -nodes -subj "/CN=AIStudioPhoneBridge"`,
          { stdio: "ignore" },
        );
      } catch (certErr) {
        console.warn(
          "[WARN][ui/electron/main.cjs::startBuiltInPhoneBridgeServers] OpenSSL self-signed cert skipped:",
          certErr.message,
        );
      }
    }

    if (fs.existsSync(keyPath) && fs.existsSync(certPath)) {
      const credentials = {
        key: fs.readFileSync(keyPath),
        cert: fs.readFileSync(certPath),
      };
      phoneHttpsServer = https.createServer(
        credentials,
        handlePhoneBridgeRequest,
      );
      phoneHttpsServer.on("error", (err) => {
        console.warn(
          `[WARN][ui/electron/main.cjs::startBuiltInPhoneBridgeServers] HTTPS :${HTTPS_BRIDGE_PORT} notice:`,
          err.message,
        );
      });
      phoneHttpsServer.listen(HTTPS_BRIDGE_PORT, "0.0.0.0", () => {
        console.log(
          `[DEBUG][ui/electron/main.cjs::startBuiltInPhoneBridgeServers] USB-C Phone Bridge HTTPS server active on port ${HTTPS_BRIDGE_PORT}`,
        );
      });
    }
  } catch (err) {
    console.error(
      "[ERROR][ui/electron/main.cjs::startBuiltInPhoneBridgeServers]",
      err,
    );
  }
}

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

// Bridges USB Type-C cable (ADB reverse :8766 + forward :8080/:4747 + USB Tethering IP detection)
function setupUsbTypeCBridge(autoLaunchPhoneBrowser = false) {
  return new Promise((resolve) => {
    const adbBin = resolveAdbBinary();
    const netInterfaces = getSystemNetworkAddresses();
    const usbTetherIface = netInterfaces.find((n) => n.isUsbTether);

    execFile(adbBin, ["devices"], { timeout: 1500 }, (err, stdout) => {
      const lines = (stdout || "")
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter(
          (l) => l && !l.startsWith("List of devices") && l.includes("device"),
        );

      if (lines.length > 0) {
        console.log(
          `[DEBUG][ui/electron/main.cjs::setupUsbTypeCBridge] Found ADB USB-C device(s): ${lines.join(", ")}`,
        );
        execFile(
          adbBin,
          ["reverse", `tcp:${HTTP_BRIDGE_PORT}`, `tcp:${HTTP_BRIDGE_PORT}`],
          { timeout: 1000 },
          () => {},
        );
        execFile(
          adbBin,
          ["forward", "tcp:8080", "tcp:8080"],
          { timeout: 1000 },
          () => {},
        );
        execFile(
          adbBin,
          ["forward", "tcp:4747", "tcp:4747"],
          { timeout: 1000 },
          () => {},
        );

        if (autoLaunchPhoneBrowser) {
          execFile(
            adbBin,
            [
              "shell",
              "am",
              "start",
              "-a",
              "android.intent.action.VIEW",
              "-d",
              `http://127.0.0.1:${HTTP_BRIDGE_PORT}/camera`,
            ],
            { timeout: 1500 },
            () => {},
          );
        }

        return resolve({
          ok: true,
          mode: "adb_usb_c",
          devices: lines,
          phoneActive: Date.now() - lastPhoneFrameTimestamp < 3000,
          usbUrl: `http://127.0.0.1:${HTTP_BRIDGE_PORT}/camera`,
          interfaces: netInterfaces,
          message: `USB-C Phone connected (${lines[0].split(/\s+/)[0]})! Tunnel active on port ${HTTP_BRIDGE_PORT}.`,
        });
      }

      if (usbTetherIface) {
        console.log(
          `[DEBUG][ui/electron/main.cjs::setupUsbTypeCBridge] Detected USB-C Tethered Phone interface: ${usbTetherIface.address}`,
        );
        return resolve({
          ok: true,
          mode: "usb_tether",
          devices: [usbTetherIface.iface],
          phoneActive: Date.now() - lastPhoneFrameTimestamp < 3000,
          usbUrl: usbTetherIface.httpsUrl,
          interfaces: netInterfaces,
          message: `USB-C Tethered Phone detected! Open ${usbTetherIface.httpsUrl} on your phone browser.`,
        });
      }

      const primaryLan = netInterfaces[0];
      resolve({
        ok: Date.now() - lastPhoneFrameTimestamp < 3000,
        mode: "bridge_ready",
        devices: [],
        phoneActive: Date.now() - lastPhoneFrameTimestamp < 3000,
        usbUrl: primaryLan
          ? primaryLan.httpsUrl
          : `http://127.0.0.1:${HTTP_BRIDGE_PORT}/camera`,
        interfaces: netInterfaces,
        message: primaryLan
          ? `Built-In Phone Camera Bridge Ready at ${primaryLan.httpsUrl} (or enable USB Tethering / USB Debugging on phone)`
          : "Built-In USB-C Phone Bridge Ready on port 8766.",
      });
    });
  });
}

async function requestMediaPermissions() {
  if (process.platform === "darwin") {
    try {
      const camStatus = await systemPreferences.askForMediaAccess("camera");
      const micStatus = await systemPreferences.askForMediaAccess("microphone");
      console.log(
        `[DEBUG][ui/electron/main.cjs::requestMediaPermissions] macOS Camera=${camStatus}, Mic=${micStatus}`,
      );
    } catch (err) {
      console.warn(
        "[WARN][ui/electron/main.cjs::requestMediaPermissions] Media permission warning:",
        err,
      );
    }
  }
}

function startEngine() {
  const enginePath = getEngineBinaryPath();
  const projectRoot = path.join(__dirname, "../../");

  console.log(
    `[DEBUG][ui/electron/main.cjs::startEngine] Spawning C++ AI Engine (${process.platform}): ${enginePath}`,
  );

  try {
    engineProcess = spawn(enginePath, [], {
      cwd: projectRoot,
      stdio: "inherit",
      windowsHide: true,
      env: {
        ...process.env,
        OPENCV_VIDEOIO_PRIORITY_MSMF: "0",
        OPENCV_FFMPEG_CAPTURE_OPTIONS:
          "fflags;nobuffer|flags;low_delay|rtsp_transport;tcp",
      },
    });

    engineProcess.on("error", (err) => {
      console.error(
        "[ERROR][ui/electron/main.cjs::startEngine] Engine process error:",
        err,
      );
    });

    engineProcess.on("exit", (code, signal) => {
      console.log(
        `[DEBUG][ui/electron/main.cjs::startEngine] AI Engine exited (code: ${code}, signal: ${signal})`,
      );
      engineProcess = null;
    });
  } catch (err) {
    console.error(
      "[ERROR][ui/electron/main.cjs::startEngine] Engine launch failed:",
      err,
    );
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
      console.warn(
        "[WARN][ui/electron/main.cjs::stopEngine] Error stopping engine:",
        e,
      );
    }
    engineProcess = null;
  }
}

function createWindow() {
  const preloadPath = resolvePreloadPath();

  mainWindow = new BrowserWindow({
    width: 1380,
    height: 880,
    minWidth: 1100,
    minHeight: 700,
    backgroundColor: "#020617",
    title: "AI Studio DeepLive Suite",
    show: false,
    webPreferences: {
      preload: preloadPath,
      contextIsolation: true,
      nodeIntegration: false,
      webSecurity: false,
      backgroundThrottling: false,
      spellcheck: false,
    },
  });

  mainWindow.once("ready-to-show", () => {
    console.log(
      "[DEBUG][ui/electron/main.cjs::createWindow] Electron mainWindow ready-to-show.",
    );
    mainWindow.show();
  });

  if (process.env.NODE_ENV === "development") {
    mainWindow.loadURL("http://localhost:5173");
  } else {
    const indexHtml = path.join(__dirname, "../dist/index.html");
    console.log(
      `[DEBUG][ui/electron/main.cjs::createWindow] Loading production UI: ${indexHtml}`,
    );
    mainWindow.loadFile(indexHtml);
  }
}

ipcMain.handle("dialog:openFile", async (event, options) => {
  console.log(
    "[DEBUG][ui/electron/main.cjs::dialog:openFile] Opening native file dialog:",
    JSON.stringify(options),
  );
  const { canceled, filePaths } = await dialog.showOpenDialog(mainWindow, {
    properties: ["openFile"],
    filters: options?.filters || [{ name: "All Files", extensions: ["*"] }],
  });
  const chosen = canceled || filePaths.length === 0 ? null : filePaths[0];
  console.log(
    `[DEBUG][ui/electron/main.cjs::dialog:openFile] Selected file: ${chosen || "CANCELED"}`,
  );
  return chosen;
});

ipcMain.handle("usb:bridgeTypeC", async (event, autoLaunch = true) => {
  console.log(
    `[DEBUG][ui/electron/main.cjs::usb:bridgeTypeC] Bridging USB Type-C Phone Camera (autoLaunch=${autoLaunch})...`,
  );
  return await setupUsbTypeCBridge(Boolean(autoLaunch));
});

ipcMain.handle("usb:getPhoneBridgeStatus", async () => {
  return {
    phoneStreaming: Date.now() - lastPhoneFrameTimestamp < 3000,
    framesReceived: phoneStreamFrameCount,
    httpPort: HTTP_BRIDGE_PORT,
    httpsPort: HTTPS_BRIDGE_PORT,
    interfaces: getSystemNetworkAddresses(),
  };
});

app.whenReady().then(async () => {
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
  startBuiltInPhoneBridgeServers();
  await setupUsbTypeCBridge(false);
  startEngine();
  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  stopEngine();
  if (phoneHttpServer) phoneHttpServer.close();
  if (phoneHttpsServer) phoneHttpsServer.close();
  if (process.platform !== "darwin") app.quit();
});

app.on("before-quit", () => {
  stopEngine();
});
