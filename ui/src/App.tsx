import React, { useState, useEffect, useRef, useCallback } from 'react';
import { Sidebar } from './components/Sidebar';
import { NavigationTab, VoiceProfile, EngineSettings } from './types';
import { EngineIPCClient, CachedModelItem } from './services/EngineIPCClient';

type CameraDevice = {
    id: string;
    label: string;
    index: number;
    endpoint?: string;
    isStream?: boolean;
    isVirtual?: boolean;
};

const DEFAULT_FALLBACK_SOURCES: CameraDevice[] = [
    {
        id: 'auto_detect',
        label: '⚡ Auto-Detect Best Camera / USB Type-C',
        index: -1,
        endpoint: 'auto',
        isStream: false
    },
    {
        id: 'usb_type_c_8080',
        label: '🔌 Direct USB Type-C Stream (ADB :8080)',
        index: -1,
        endpoint: 'http://127.0.0.1:8080/video',
        isStream: true
    },
    {
        id: 'usb_type_c_4747',
        label: '🔌 Direct USB Type-C Stream (DroidCam :4747)',
        index: -1,
        endpoint: 'http://127.0.0.1:4747/video',
        isStream: true
    }
];

export function App() {
    const [activeTab, setActiveTab] = useState<NavigationTab>('dashboard');
    const [engineRunning, setEngineRunning] = useState<boolean>(false);
    const [logs, setLogs] = useState<string[]>([]);
    const [engineLoading, setEngineLoading] = useState<boolean>(false);

    const [cameras, setCameras] = useState<CameraDevice[]>(DEFAULT_FALLBACK_SOURCES);
    const [selectedCam, setSelectedCam] = useState<CameraDevice>(DEFAULT_FALLBACK_SOURCES[0]);

    const [customStreamUrl, setCustomStreamUrl] = useState<string>('http://127.0.0.1:8080/video');
    const [showUsbPanel, setShowUsbPanel] = useState<boolean>(false);
    const [liveFps, setLiveFps] = useState<number>(0);
    const [activeEngineSource, setActiveEngineSource] = useState<string>('None');

    // Real-Time Body Kinematics & Audio Telemetry State
    const [kinematics, setKinematics] = useState({
        leftArmRaised: false,
        rightArmRaised: false,
        handsHoldingBody: false,
        fullBodyVisible: true,
        headTilt: 0,
        speechEnergy: 0,
        audioPeak: 0,
        routedFrames: 0,
        cloudStatus: 'UpToDate'
    });

    const [backgroundMode, setBackgroundMode] = useState<'disabled' | 'blur' | 'virtual' | 'transparent'>('disabled');
    const [voicePreviewActive, setVoicePreviewActive] = useState<boolean>(false);
    const [cachedModels, setCachedModels] = useState<CachedModelItem[]>([]);

    const sourceVideoRef = useRef<HTMLVideoElement>(null);
    const targetVideoRef = useRef<HTMLVideoElement>(null);
    const activeStreamRef = useRef<MediaStream | null>(null);

    const [activeProfileId, setActiveProfileId] = useState<string>('user_custom_profile');
    const [avatarImagePath, setAvatarImagePath] = useState<string>('');
    const [avatarPreviewUrl, setAvatarPreviewUrl] = useState<string>('');

    const [settings, setSettings] = useState<EngineSettings>({
        pitchShift: 0,
        indexRate: 0.85,
        protectRate: 0.33,
        faceSwapEnabled: true,
        lipSyncEnabled: true,
        backgroundMattingEnabled: false,
        garmentOverlayEnabled: false,
        performanceMode: 'Performance'
    });

    const [voices, setVoices] = useState<VoiceProfile[]>([
        {
            id: 'user_custom_profile',
            displayName: 'Custom Studio Voice (Default)',
            sourcePath: 'models/sample_source.wav',
            modelPath: 'models/user_custom_profile.onnx',
            isCached: true,
            createdAt: new Date().toISOString()
        }
    ]);

    const addLog = useCallback((message: string) => {
        setLogs((prev) => [`[${new Date().toLocaleTimeString()}] ${message}`, ...prev.slice(0, 49)]);
    }, []);

    const releasePreviewStream = useCallback(() => {
        if (activeStreamRef.current) {
            activeStreamRef.current.getTracks().forEach((t) => t.stop());
            activeStreamRef.current = null;
        }
        if (sourceVideoRef.current) {
            sourceVideoRef.current.srcObject = null;
            sourceVideoRef.current.removeAttribute('src');
        }
    }, []);

    // 1. Unified Cross-Platform Discovery (WebRTC + C++ Hardware & USB Type-C Probe)
    const scanCameras = useCallback(async () => {
        try {
            // @ts-ignore
            if (window.electronAPI?.bridgeUsbTypeC) {
                // @ts-ignore
                await window.electronAPI.bridgeUsbTypeC().catch(() => null);
            }

            if (!activeStreamRef.current && !engineRunning) {
                const tempStream = await navigator.mediaDevices.getUserMedia({ video: true }).catch(() => null);
                if (tempStream) tempStream.getTracks().forEach((t) => t.stop());
            }

            const discoveredMap = new Map<string, CameraDevice>();
            discoveredMap.set('auto_detect', DEFAULT_FALLBACK_SOURCES[0]);

            const devices = await navigator.mediaDevices.enumerateDevices().catch(() => []);
            const rawVideoDevices = devices.filter((d) => d.kind === 'videoinput');

            rawVideoDevices.forEach((dev, hwIndex) => {
                const label = dev.label || `Camera Device #${hwIndex} (Index ${hwIndex})`;
                if (!label.toLowerCase().includes('obs virtual camera')) {
                    const id = dev.deviceId || `cam_${hwIndex}`;
                    discoveredMap.set(`hw_${hwIndex}`, {
                        id,
                        label,
                        index: hwIndex,
                        endpoint: String(hwIndex),
                        isStream: false,
                        isVirtual: label.toLowerCase().includes('camo') || label.toLowerCase().includes('virtual')
                    });
                }
            });

            const cppScan = await EngineIPCClient.scanCameras();
            if (cppScan.status === 'ok' && Array.isArray(cppScan.devices)) {
                cppScan.devices.forEach((dev) => {
                    if (dev.is_stream) {
                        discoveredMap.set(`stream_${dev.endpoint}`, {
                            id: `stream_${dev.endpoint}`,
                            label: `🔌 ${dev.name}`,
                            index: -1,
                            endpoint: dev.endpoint,
                            isStream: true,
                            isVirtual: false
                        });
                    } else if (!discoveredMap.has(`hw_${dev.index}`)) {
                        discoveredMap.set(`hw_${dev.index}`, {
                            id: `cpp_cam_${dev.index}`,
                            label: `${dev.name} (Index ${dev.index})`,
                            index: dev.index,
                            endpoint: dev.endpoint,
                            isStream: false,
                            isVirtual: dev.is_virtual
                        });
                    }
                });
            }

            DEFAULT_FALLBACK_SOURCES.slice(1).forEach((fallback) => {
                if (!discoveredMap.has(`stream_${fallback.endpoint}`)) {
                    discoveredMap.set(fallback.id, fallback);
                }
            });

            const finalCameras = Array.from(discoveredMap.values());
            setCameras(finalCameras);

            setSelectedCam((prev) => {
                const stillExists = finalCameras.find((c) => c.id === prev.id || (c.endpoint && c.endpoint === prev.endpoint));
                if (stillExists && prev.id !== 'auto_detect') return stillExists;

                const preferred = finalCameras.find(
                    (c) =>
                        c.id.startsWith('stream_http') ||
                        c.label.toLowerCase().includes('camo') ||
                        c.label.toLowerCase().includes('usb') ||
                        c.label.toLowerCase().includes('external') ||
                        c.index >= 0
                );
                return preferred || finalCameras[0];
            });

            const hwCount = finalCameras.filter((c) => c.index >= 0).length;
            if (hwCount > 0) {
                addLog(`📷 Detected ${hwCount} OS camera(s) + Direct USB Type-C Fallback ready.`);
            } else {
                setShowUsbPanel(true);
                addLog(`🔌 No OS webcam detected. Direct USB Type-C / Stream Fallback Mode unlocked!`);
            }
        } catch (err: any) {
            addLog(`Camera scan notice: ${err.message}`);
        }
    }, [engineRunning, addLog]);

    useEffect(() => {
        scanCameras();
        EngineIPCClient.syncCloudModels().then((res) => {
            if (res.models) setCachedModels(res.models);
        });
        if (navigator.mediaDevices?.addEventListener) {
            navigator.mediaDevices.addEventListener('devicechange', scanCameras);
            return () => {
                navigator.mediaDevices.removeEventListener('devicechange', scanCameras);
            };
        }
    }, [scanCameras]);

    // Real-Time Telemetry Polling (FPS, Hand Raising, Full-Body Status, Lip-Sync Energy)
    useEffect(() => {
        const timer = setInterval(async () => {
            const status = await EngineIPCClient.getCameraStatus();
            if (status.status === 'ok') {
                if (typeof status.fps === 'number') setLiveFps(status.fps);
                if (status.active_source) setActiveEngineSource(status.active_source);
                setKinematics({
                    leftArmRaised: Boolean(status.left_arm_raised),
                    rightArmRaised: Boolean(status.right_arm_raised),
                    handsHoldingBody: Boolean(status.hands_holding_body),
                    fullBodyVisible: status.full_body_visible ?? true,
                    headTilt: status.head_tilt ?? 0,
                    speechEnergy: status.speech_energy ?? 0,
                    audioPeak: status.audio_peak ?? 0,
                    routedFrames: status.routed_frames ?? 0,
                    cloudStatus: status.cloud_status || 'UpToDate'
                });
            }
        }, 600);
        return () => clearInterval(timer);
    }, []);

    // High-FPS Zero-Lag Raw Webcam or Local File Preview Stream
    useEffect(() => {
        let cancelled = false;

        const startPreview = async () => {
            if (!selectedCam || engineRunning || engineLoading) return;

            releasePreviewStream();

            if (selectedCam.isStream || selectedCam.index < 0) {
                if (
                    selectedCam.endpoint &&
                    sourceVideoRef.current &&
                    (selectedCam.endpoint.startsWith('file://') ||
                        selectedCam.endpoint.endsWith('.mp4') ||
                        selectedCam.endpoint.endsWith('.mov') ||
                        selectedCam.endpoint.endsWith('.webm'))
                ) {
                    sourceVideoRef.current.src = selectedCam.endpoint.startsWith('file://')
                        ? selectedCam.endpoint
                        : `file://${selectedCam.endpoint}`;
                    sourceVideoRef.current.loop = true;
                    sourceVideoRef.current.play().catch(() => null);
                }
                return;
            }

            try {
                const useExactDevice =
                    selectedCam.id &&
                    !selectedCam.id.startsWith('cpp_cam_') &&
                    selectedCam.id !== 'default' &&
                    selectedCam.id !== 'auto_detect';

                const constraints: MediaStreamConstraints = {
                    audio: false,
                    video: useExactDevice
                        ? {
                            deviceId: { exact: selectedCam.id },
                            width: { ideal: 1280 },
                            height: { ideal: 720 },
                            frameRate: { ideal: 60, min: 30 }
                        }
                        : {
                            width: { ideal: 1280 },
                            height: { ideal: 720 },
                            frameRate: { ideal: 60, min: 30 }
                        }
                };

                const stream = await navigator.mediaDevices.getUserMedia(constraints);
                if (cancelled) {
                    stream.getTracks().forEach((t) => t.stop());
                    return;
                }

                activeStreamRef.current = stream;
                if (sourceVideoRef.current) {
                    sourceVideoRef.current.srcObject = stream;
                }
            } catch {
                // Silent fallback if OS has no physical camera
            }
        };

        startPreview();

        return () => {
            cancelled = true;
            releasePreviewStream();
        };
    }, [selectedCam, engineRunning, engineLoading, releasePreviewStream]);

    // File Pickers & Feature Handlers
    const handleNativeAvatarPick = async () => {
        // @ts-ignore
        if (window.electronAPI?.openFileDialog) {
            // @ts-ignore
            const selectedPath = await window.electronAPI.openFileDialog({
                filters: [{ name: 'Images', extensions: ['jpg', 'jpeg', 'png', 'webp'] }]
            });
            if (selectedPath) {
                setAvatarImagePath(selectedPath);
                setAvatarPreviewUrl(`file://${selectedPath}`);
                addLog(`👤 Target avatar selected & embedded: ${selectedPath}`);
                await EngineIPCClient.setFaceSwapAvatar(selectedPath);
            }
        }
    };

    const handleVirtualBgPick = async () => {
        // @ts-ignore
        if (window.electronAPI?.openFileDialog) {
            // @ts-ignore
            const selectedPath = await window.electronAPI.openFileDialog({
                filters: [{ name: 'Background Images', extensions: ['jpg', 'jpeg', 'png', 'webp'] }]
            });
            if (selectedPath) {
                setBackgroundMode('virtual');
                setSettings((s) => ({ ...s, backgroundMattingEnabled: true }));
                await EngineIPCClient.toggleBackgroundMatting(true, 'virtual', selectedPath);
                addLog(`🖼️ Virtual Background loaded with real-time body lighting harmonization: ${selectedPath}`);
            }
        }
    };

    const handleBackgroundModeChange = async (mode: 'disabled' | 'blur' | 'virtual' | 'transparent') => {
        setBackgroundMode(mode);
        const enabled = mode !== 'disabled';
        setSettings((s) => ({ ...s, backgroundMattingEnabled: enabled }));
        await EngineIPCClient.toggleBackgroundMatting(enabled, mode);
        addLog(`🎬 Background mode set to: ${mode.toUpperCase()}`);
    };

    const handleVisionToggle = (key: 'faceSwapEnabled' | 'lipSyncEnabled' | 'garmentOverlayEnabled') => {
        setSettings((prev) => {
            const next = { ...prev, [key]: !prev[key] };
            EngineIPCClient.sendAsyncCommand({
                command: 'SET_VISION_CONFIG',
                face_swap: next.faceSwapEnabled,
                lip_sync: next.lipSyncEnabled,
                garment_overlay: next.garmentOverlayEnabled,
                body_tracking: true
            });
            return next;
        });
    };

    const handleVoiceParamChange = (pitch: number, indexRate: number, protectRate: number) => {
        setSettings((prev) => ({
            ...prev,
            pitchShift: pitch,
            indexRate,
            protectRate
        }));
        EngineIPCClient.sendAsyncCommand({
            command: 'SET_VOICE_PARAMS',
            pitch,
            index_rate: indexRate,
            protect_rate: protectRate
        });
    };

    const handleNativeVideoSourcePick = async () => {
        // @ts-ignore
        if (window.electronAPI?.openFileDialog) {
            // @ts-ignore
            const selectedPath = await window.electronAPI.openFileDialog({
                filters: [{ name: 'Video Files', extensions: ['mp4', 'mov', 'mkv', 'avi', 'webm'] }]
            });
            if (selectedPath) {
                const fileName = selectedPath.split(/[\\/]/).pop() || 'Video Source';
                const fileCam: CameraDevice = {
                    id: `file_${Date.now()}`,
                    label: `🎬 Video: ${fileName}`,
                    index: -1,
                    endpoint: selectedPath,
                    isStream: true
                };
                setCameras((prev) => [fileCam, ...prev]);
                setSelectedCam(fileCam);
                setCustomStreamUrl(selectedPath);
                addLog(`🎬 Selected local video source: ${selectedPath}`);
            }
        }
    };

    const handleConnectUsbTypeC = async () => {
        const cleanUrl = customStreamUrl.trim() || 'http://127.0.0.1:8080/video';
        addLog(`🔌 Bridging USB Type-C & binding endpoint: ${cleanUrl}...`);

        // @ts-ignore
        if (window.electronAPI?.bridgeUsbTypeC) {
            // @ts-ignore
            const bridgeRes = await window.electronAPI.bridgeUsbTypeC().catch(() => null);
            if (bridgeRes?.message) {
                addLog(`🔌 ${bridgeRes.message}`);
            }
        }

        const usbCam: CameraDevice = {
            id: `usb_custom_${cleanUrl}`,
            label: `🔌 USB Type-C (${cleanUrl})`,
            index: -1,
            endpoint: cleanUrl,
            isStream: true
        };

        setCameras((prev) => {
            const exists = prev.some((c) => c.endpoint === cleanUrl);
            return exists ? prev : [usbCam, ...prev];
        });
        setSelectedCam(usbCam);

        if (engineRunning) {
            const res = await EngineIPCClient.connectUsbOrStream(cleanUrl, -1);
            if (res.status === 'ok') {
                setActiveEngineSource(res.active_source || cleanUrl);
                addLog(`✅ Live switched to USB Type-C stream: ${res.active_source || cleanUrl}`);
            } else {
                addLog(`⚠️ Stream not responding yet at ${cleanUrl}. Ensure phone stream app is active.`);
            }
        } else {
            addLog(`✅ USB Type-C endpoint locked (${cleanUrl}). Click START ENGINE to go live!`);
        }
    };

    const handleNativeVoicePick = async () => {
        // @ts-ignore
        if (window.electronAPI?.openFileDialog) {
            // @ts-ignore
            const selectedPath = await window.electronAPI.openFileDialog({
                filters: [{ name: 'Voice Models', extensions: ['wav', 'mp3', 'flac', 'm4a', 'onnx'] }]
            });
            if (selectedPath) {
                const fileName = selectedPath.split(/[\\/]/).pop() || 'Custom Voice';
                const newVoiceId = fileName.replace(/\.[^/.]+$/, '');
                setVoices((prev) => [
                    ...prev.filter((v) => v.id !== newVoiceId),
                    {
                        id: newVoiceId,
                        displayName: fileName,
                        sourcePath: selectedPath,
                        modelPath: selectedPath,
                        isCached: true,
                        createdAt: new Date().toISOString()
                    }
                ]);
                setActiveProfileId(newVoiceId);
                addLog(`🗣️ Voice model loaded & compiled: ${selectedPath}`);
                await EngineIPCClient.setVoiceProfile(selectedPath);
            }
        }
    };

    const handleToggleEngine = async () => {
        if (!engineRunning) {
            if (!avatarImagePath) {
                return alert('Please select a Face Swap Target Picture first!');
            }

            setEngineLoading(true);
            addLog(`🚀 Releasing UI preview lock & handing off "${selectedCam.label}" to C++ Engine...`);

            try {
                // @ts-ignore
                if (window.electronAPI?.bridgeUsbTypeC) {
                    // @ts-ignore
                    await window.electronAPI.bridgeUsbTypeC().catch(() => null);
                }

                releasePreviewStream();
                await new Promise((resolve) => setTimeout(resolve, 60));

                await EngineIPCClient.setFaceSwapAvatar(avatarImagePath);

                const isStreamSource = Boolean(selectedCam?.isStream && selectedCam?.endpoint && selectedCam.endpoint !== 'auto');
                const camIndex = selectedCam && !isStreamSource ? selectedCam.index : -1;
                const streamEndpoint = isStreamSource ? (selectedCam.endpoint || '') : '';

                const camRes = await EngineIPCClient.sendCommand(
                    {
                        command: 'START_CAMERA',
                        index: camIndex,
                        camera_index: camIndex,
                        url: streamEndpoint,
                        endpoint: streamEndpoint
                    },
                    3500
                );

                const callRes = await EngineIPCClient.sendCommand({
                    command: 'START_CALL',
                    profile: activeProfileId,
                    pitch: settings.pitchShift
                });

                if (camRes.status !== 'ok' || callRes?.status === 'error') {
                    throw new Error(
                        camRes.message ||
                        `Could not open "${selectedCam.label}". Connect a USB Type-C camera stream or select a Video File fallback.`
                    );
                }

                setActiveEngineSource(camRes.active_source || selectedCam.label);
                setEngineRunning(true);
                addLog(`✅ C++ Engine Live on [${camRes.active_source || selectedCam.label}] (Triple-Buffered Zero-Lag Mode).`);

                const devices = await navigator.mediaDevices.enumerateDevices().catch(() => []);
                const virtualOut = devices.find(
                    (d) =>
                        d.kind === 'videoinput' &&
                        (d.label.toLowerCase().includes('obs') ||
                            (d.label.toLowerCase().includes('virtual') &&
                                !d.label.toLowerCase().includes('camo')))
                );

                if (virtualOut && targetVideoRef.current) {
                    const outStream = await navigator.mediaDevices.getUserMedia({
                        video: {
                            deviceId: { exact: virtualOut.deviceId },
                            width: { ideal: 1280 },
                            height: { ideal: 720 },
                            frameRate: { ideal: 60 }
                        }
                    });
                    targetVideoRef.current.srcObject = outStream;
                    addLog(`✅ Output bridged via ${virtualOut.label}.`);
                }
            } catch (err: any) {
                setShowUsbPanel(true);
                addLog(`❌ Engine Startup Notice: ${err.message}`);
                alert(`Engine could not open the camera source:\n${err.message}\n\nTip: Use the Direct USB Type-C / Video Fallback bar below Source 1!`);
                setEngineRunning(false);
            } finally {
                setEngineLoading(false);
            }
        } else {
            addLog('🛑 Stopping C++ AI Pipeline...');
            await EngineIPCClient.stopCamera();
            await EngineIPCClient.sendCommand({ command: 'STOP_CALL' });

            if (targetVideoRef.current && targetVideoRef.current.srcObject) {
                const s = targetVideoRef.current.srcObject as MediaStream;
                s.getTracks().forEach((t) => t.stop());
                targetVideoRef.current.srcObject = null;
            }

            setEngineRunning(false);
            setEngineLoading(false);
            setLiveFps(0);
            addLog('Hardware released back to UI preview.');
        }
    };

    const isHttpMjpegSelected =
        !engineRunning &&
        !engineLoading &&
        selectedCam?.isStream &&
        selectedCam?.endpoint &&
        selectedCam.endpoint.startsWith('http');

    return (
        <div
            style={{
                display: 'flex',
                height: '100vh',
                background: '#020617',
                color: '#f8fafc',
                fontFamily: 'system-ui, -apple-system, sans-serif',
                overflow: 'hidden'
            }}
        >
            <Sidebar
                activeTab={activeTab}
                setActiveTab={setActiveTab}
                callActive={engineRunning}
                activeSource={activeEngineSource}
                fps={liveFps}
            />

            <main style={{ flex: 1, display: 'flex', flexDirection: 'column', height: '100vh', overflowY: 'auto', padding: '28px 32px' }}>
                {/* ==================== TAB 1: LIVE STUDIO CALL DASHBOARD ==================== */}
                {activeTab === 'dashboard' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header
                            style={{
                                borderBottom: '1px solid #1e293b',
                                paddingBottom: '14px',
                                display: 'flex',
                                justifyContent: 'space-between',
                                alignItems: 'center'
                            }}
                        >
                            <div>
                                <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8', fontWeight: 700 }}>
                                    AI Studio DeepLive Suite
                                </h1>
                                <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>
                                    Zero-Lag Triple-Buffered C++20 Pipeline • Full-Body, Hands, Skin Blending, Lip-Sync & Real-Time RVC Voice
                                </p>
                            </div>
                            <button
                                onClick={handleToggleEngine}
                                disabled={engineLoading}
                                style={{
                                    background: engineRunning ? '#dc2626' : '#16a34a',
                                    color: '#fff',
                                    border: 'none',
                                    padding: '12px 24px',
                                    borderRadius: '8px',
                                    fontWeight: 'bold',
                                    fontSize: '15px',
                                    cursor: engineLoading ? 'wait' : 'pointer',
                                    opacity: engineLoading ? 0.7 : 1
                                }}
                            >
                                {engineLoading ? '⌛ Initializing Engine...' : engineRunning ? '🛑 STOP ENGINE' : '▶ START ENGINE (GO LIVE)'}
                            </button>
                        </header>

                        {/* SPLIT-SCREEN PREVIEW */}
                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>
                            {/* SOURCE: RAW WEBCAM OR DIRECT USB TYPE-C */}
                            <div style={{ background: '#0f172a', padding: '16px', borderRadius: '16px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', gap: '10px' }}>
                                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: '8px' }}>
                                    <h3 style={{ margin: 0, fontSize: '15px', color: '#e2e8f0' }}>1. Source (You)</h3>
                                    <div style={{ display: 'flex', gap: '6px', alignItems: 'center' }}>
                                        <select
                                            value={selectedCam?.id || ''}
                                            onChange={(e) => {
                                                const cam = cameras.find((c) => c.id === e.target.value);
                                                if (cam) {
                                                    setSelectedCam(cam);
                                                    if (cam.isStream && cam.endpoint) {
                                                        setCustomStreamUrl(cam.endpoint);
                                                    }
                                                }
                                            }}
                                            disabled={engineRunning || engineLoading}
                                            style={{
                                                padding: '6px 8px',
                                                background: '#020617',
                                                border: '1px solid #334155',
                                                color: '#38bdf8',
                                                borderRadius: '6px',
                                                fontSize: '12px',
                                                maxWidth: '210px'
                                            }}
                                        >
                                            {cameras.map((cam) => (
                                                <option key={cam.id} value={cam.id}>
                                                    {cam.index >= 0 ? `[${cam.index}] ${cam.label}` : cam.label}
                                                </option>
                                            ))}
                                        </select>
                                        <button
                                            onClick={scanCameras}
                                            disabled={engineRunning || engineLoading}
                                            title="Rescan cameras & USB Type-C ports"
                                            style={{
                                                background: '#1e293b',
                                                color: '#38bdf8',
                                                border: '1px solid #334155',
                                                borderRadius: '6px',
                                                padding: '6px 9px',
                                                fontSize: '12px',
                                                cursor: 'pointer'
                                            }}
                                        >
                                            🔄
                                        </button>
                                        <button
                                            onClick={() => setShowUsbPanel((prev) => !prev)}
                                            title="Direct USB Type-C / Stream Fallback"
                                            style={{
                                                background: showUsbPanel ? '#0284c7' : '#1e293b',
                                                color: '#f8fafc',
                                                border: '1px solid #38bdf8',
                                                borderRadius: '6px',
                                                padding: '6px 9px',
                                                fontSize: '11px',
                                                fontWeight: 600,
                                                cursor: 'pointer'
                                            }}
                                        >
                                            🔌 USB-C
                                        </button>
                                    </div>
                                </div>

                                {showUsbPanel && (
                                    <div
                                        style={{
                                            display: 'flex',
                                            gap: '6px',
                                            alignItems: 'center',
                                            background: '#020617',
                                            padding: '8px',
                                            borderRadius: '8px',
                                            border: '1px solid #1e293b'
                                        }}
                                    >
                                        <input
                                            type="text"
                                            value={customStreamUrl}
                                            onChange={(e) => setCustomStreamUrl(e.target.value)}
                                            placeholder="USB-C Stream URL (e.g. http://127.0.0.1:8080/video)"
                                            style={{
                                                flex: 1,
                                                background: '#0f172a',
                                                color: '#e2e8f0',
                                                border: '1px solid #334155',
                                                borderRadius: '6px',
                                                padding: '6px 8px',
                                                fontSize: '11px'
                                            }}
                                        />
                                        <button
                                            onClick={handleConnectUsbTypeC}
                                            style={{
                                                background: '#0284c7',
                                                color: '#fff',
                                                border: 'none',
                                                borderRadius: '6px',
                                                padding: '6px 10px',
                                                fontSize: '11px',
                                                fontWeight: 600,
                                                cursor: 'pointer',
                                                whiteSpace: 'nowrap'
                                            }}
                                        >
                                            Connect USB-C
                                        </button>
                                        <button
                                            onClick={handleNativeVideoSourcePick}
                                            title="Use a local .mp4/.mov video file as camera source"
                                            style={{
                                                background: '#334155',
                                                color: '#f8fafc',
                                                border: 'none',
                                                borderRadius: '6px',
                                                padding: '6px 10px',
                                                fontSize: '11px',
                                                fontWeight: 600,
                                                cursor: 'pointer',
                                                whiteSpace: 'nowrap'
                                            }}
                                        >
                                            🎬 Video File
                                        </button>
                                    </div>
                                )}

                                <div
                                    style={{
                                        background: '#020617',
                                        borderRadius: '12px',
                                        border: '1px solid #1e293b',
                                        height: '260px',
                                        overflow: 'hidden',
                                        position: 'relative'
                                    }}
                                >
                                    {!engineRunning && !engineLoading ? (
                                        isHttpMjpegSelected ? (
                                            <img
                                                src={selectedCam.endpoint}
                                                alt="USB Type-C Live Preview"
                                                style={{
                                                    width: '100%',
                                                    height: '100%',
                                                    objectFit: 'cover',
                                                    transform: 'scaleX(-1) translateZ(0)'
                                                }}
                                            />
                                        ) : (
                                            <video
                                                ref={sourceVideoRef}
                                                autoPlay
                                                playsInline
                                                muted
                                                disablePictureInPicture
                                                style={{
                                                    width: '100%',
                                                    height: '100%',
                                                    objectFit: 'cover',
                                                    transform: 'scaleX(-1) translateZ(0)',
                                                    willChange: 'transform'
                                                }}
                                            />
                                        )
                                    ) : (
                                        <div
                                            style={{
                                                display: 'flex',
                                                alignItems: 'center',
                                                justifyContent: 'center',
                                                height: '100%',
                                                color: '#64748b',
                                                flexDirection: 'column',
                                                gap: '6px'
                                            }}
                                        >
                                            <span style={{ fontSize: '24px' }}>⚡</span>
                                            <span style={{ fontSize: '12px', color: '#38bdf8', fontWeight: 600 }}>
                                                Direct C++ Zero-Lag Lock Active ({activeEngineSource || selectedCam.label})
                                            </span>
                                            {liveFps > 0 && (
                                                <span style={{ fontSize: '11px', color: '#22c55e', fontWeight: 700 }}>
                                                    {liveFps.toFixed(1)} FPS • 1280x720 HD
                                                </span>
                                            )}
                                        </div>
                                    )}
                                    <div
                                        style={{
                                            position: 'absolute',
                                            bottom: '10px',
                                            left: '10px',
                                            background: 'rgba(0,0,0,0.65)',
                                            padding: '4px 8px',
                                            borderRadius: '4px',
                                            fontSize: '11px',
                                            color: '#fff'
                                        }}
                                    >
                                        🔴 {engineRunning ? activeEngineSource || selectedCam.label : selectedCam.label}
                                    </div>
                                </div>
                            </div>

                            {/* TARGET: AI OUTPUT */}
                            <div style={{ background: '#0f172a', padding: '16px', borderRadius: '16px', border: '1px solid #38bdf8', display: 'flex', flexDirection: 'column', gap: '10px' }}>
                                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                                    <h3 style={{ margin: 0, fontSize: '15px', color: '#e2e8f0' }}>2. Target (AI Output)</h3>
                                    <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                                        <button
                                            onClick={handleNativeVoicePick}
                                            style={{
                                                background: '#1e293b',
                                                color: '#38bdf8',
                                                padding: '6px 10px',
                                                borderRadius: '6px',
                                                fontSize: '12px',
                                                fontWeight: 600,
                                                cursor: 'pointer',
                                                border: '1px solid #334155'
                                            }}
                                        >
                                            🗣️ Voice ({voices.length})
                                        </button>
                                        <button
                                            onClick={handleNativeAvatarPick}
                                            style={{
                                                background: '#38bdf8',
                                                color: '#020617',
                                                padding: '6px 12px',
                                                borderRadius: '6px',
                                                fontSize: '12px',
                                                fontWeight: 'bold',
                                                cursor: 'pointer',
                                                border: 'none'
                                            }}
                                        >
                                            Choose Picture
                                        </button>
                                    </div>
                                </div>
                                <div
                                    style={{
                                        background: '#020617',
                                        borderRadius: '12px',
                                        border: '1px dashed #38bdf8',
                                        height: '260px',
                                        overflow: 'hidden',
                                        position: 'relative',
                                        display: 'flex',
                                        alignItems: 'center',
                                        justifyContent: 'center'
                                    }}
                                >
                                    {!engineRunning ? (
                                        avatarPreviewUrl ? (
                                            <img
                                                src={avatarPreviewUrl}
                                                alt="Target"
                                                style={{ width: '100%', height: '100%', objectFit: 'cover', opacity: 0.9 }}
                                            />
                                        ) : (
                                            <div style={{ textAlign: 'center', color: '#64748b' }}>
                                                <span style={{ fontSize: '32px', display: 'block', marginBottom: '8px' }}>👤</span>
                                                <span style={{ fontSize: '13px' }}>Upload a picture to see face swap target.</span>
                                            </div>
                                        )
                                    ) : (
                                        <video
                                            ref={targetVideoRef}
                                            autoPlay
                                            playsInline
                                            muted
                                            disablePictureInPicture
                                            style={{
                                                width: '100%',
                                                height: '100%',
                                                objectFit: 'cover',
                                                transform: 'translateZ(0)',
                                                willChange: 'transform'
                                            }}
                                        />
                                    )}
                                    {engineLoading && (
                                        <div
                                            style={{
                                                position: 'absolute',
                                                top: '50%',
                                                left: '50%',
                                                transform: 'translate(-50%, -50%)',
                                                textAlign: 'center',
                                                background: 'rgba(2, 6, 23, 0.9)',
                                                padding: '20px',
                                                borderRadius: '8px',
                                                width: '80%'
                                            }}
                                        >
                                            <span style={{ color: '#38bdf8', fontSize: '12px', fontWeight: 'bold' }}>
                                                ⚡ Zero-Lag Hardware Handoff in Progress...
                                            </span>
                                        </div>
                                    )}
                                </div>
                            </div>
                        </div>

                        {/* REAL-TIME AI PIPELINE CONTROLS (FACE SWAP, LIP SYNC, HANDS/BODY, BACKGROUND, CLOTHING & VOICE PITCH) */}
                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>
                            <div style={{ background: '#0f172a', padding: '16px', borderRadius: '14px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', gap: '12px' }}>
                                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                                    <span style={{ fontSize: '13px', fontWeight: 700, color: '#38bdf8' }}>
                                        🎭 Real-Time Face, Full-Body & Background Controls
                                    </span>
                                    <span style={{ fontSize: '11px', color: '#22c55e' }}>
                                        {kinematics.fullBodyVisible ? '🧍 Full-Body Active' : '👤 Upper-Body Active'}
                                        {kinematics.leftArmRaised ? ' • ✋ Left Hand Up' : ''}
                                        {kinematics.rightArmRaised ? ' • ✋ Right Hand Up' : ''}
                                        {kinematics.handsHoldingBody ? ' • 🤲 Hands on Body' : ''}
                                    </span>
                                </div>
                                <div style={{ display: 'flex', flexWrap: 'wrap', gap: '8px' }}>
                                    <button
                                        onClick={() => handleVisionToggle('faceSwapEnabled')}
                                        style={{
                                            padding: '6px 12px',
                                            borderRadius: '6px',
                                            border: '1px solid #334155',
                                            background: settings.faceSwapEnabled ? '#0284c7' : '#1e293b',
                                            color: '#fff',
                                            fontSize: '12px',
                                            fontWeight: 600,
                                            cursor: 'pointer'
                                        }}
                                    >
                                        {settings.faceSwapEnabled ? '✅ Face Swap + Skin Blend' : '⬜ Face Swap Off'}
                                    </button>
                                    <button
                                        onClick={() => handleVisionToggle('lipSyncEnabled')}
                                        style={{
                                            padding: '6px 12px',
                                            borderRadius: '6px',
                                            border: '1px solid #334155',
                                            background: settings.lipSyncEnabled ? '#0284c7' : '#1e293b',
                                            color: '#fff',
                                            fontSize: '12px',
                                            fontWeight: 600,
                                            cursor: 'pointer'
                                        }}
                                    >
                                        {settings.lipSyncEnabled ? '✅ Real-Time Lip-Sync' : '⬜ Lip-Sync Off'}
                                    </button>
                                    <button
                                        onClick={() => handleVisionToggle('garmentOverlayEnabled')}
                                        style={{
                                            padding: '6px 12px',
                                            borderRadius: '6px',
                                            border: '1px solid #334155',
                                            background: settings.garmentOverlayEnabled ? '#0284c7' : '#1e293b',
                                            color: '#fff',
                                            fontSize: '12px',
                                            fontWeight: 600,
                                            cursor: 'pointer'
                                        }}
                                    >
                                        {settings.garmentOverlayEnabled ? '✅ Cloth / Torso Tone' : '👕 Cloth / Torso Tone'}
                                    </button>
                                </div>
                                <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                                    <span style={{ fontSize: '12px', color: '#94a3b8' }}>Background:</span>
                                    {(['disabled', 'blur', 'transparent'] as const).map((m) => (
                                        <button
                                            key={m}
                                            onClick={() => handleBackgroundModeChange(m)}
                                            style={{
                                                padding: '5px 10px',
                                                borderRadius: '6px',
                                                border: '1px solid #334155',
                                                background: backgroundMode === m ? '#38bdf8' : '#020617',
                                                color: backgroundMode === m ? '#020617' : '#cbd5e1',
                                                fontSize: '11px',
                                                fontWeight: 600,
                                                cursor: 'pointer'
                                            }}
                                        >
                                            {m === 'disabled' ? 'Original HD' : m === 'blur' ? 'Studio Bokeh Blur' : 'Chroma Key'}
                                        </button>
                                    ))}
                                    <button
                                        onClick={handleVirtualBgPick}
                                        style={{
                                            padding: '5px 10px',
                                            borderRadius: '6px',
                                            border: '1px solid #38bdf8',
                                            background: backgroundMode === 'virtual' ? '#38bdf8' : '#1e293b',
                                            color: backgroundMode === 'virtual' ? '#020617' : '#38bdf8',
                                            fontSize: '11px',
                                            fontWeight: 600,
                                            cursor: 'pointer'
                                        }}
                                    >
                                        🖼️ Custom BG
                                    </button>
                                </div>
                            </div>

                            {/* LIVE VOICE CLONING & PITCH CONTROLS */}
                            <div style={{ background: '#0f172a', padding: '16px', borderRadius: '14px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', gap: '10px' }}>
                                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                                    <span style={{ fontSize: '13px', fontWeight: 700, color: '#38bdf8' }}>
                                        🎙️ Real-Time Pre-Cloned Voice RVC Modulation
                                    </span>
                                    <span style={{ fontSize: '11px', color: '#94a3b8' }}>
                                        Active: <strong style={{ color: '#f8fafc' }}>{activeProfileId}</strong>
                                    </span>
                                </div>
                                <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
                                    <span style={{ fontSize: '12px', color: '#cbd5e1', minWidth: '110px' }}>
                                        Pitch ({settings.pitchShift > 0 ? `+${settings.pitchShift}` : settings.pitchShift} st):
                                    </span>
                                    <input
                                        type="range"
                                        min={-12}
                                        max={12}
                                        step={1}
                                        value={settings.pitchShift}
                                        onChange={(e) =>
                                            handleVoiceParamChange(Number(e.target.value), settings.indexRate, settings.protectRate)
                                        }
                                        style={{ flex: 1, cursor: 'pointer' }}
                                    />
                                </div>
                                <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
                                    <span style={{ fontSize: '12px', color: '#cbd5e1', minWidth: '110px' }}>
                                        TimbreMatch ({Math.round(settings.indexRate * 100)}%):
                                    </span>
                                    <input
                                        type="range"
                                        min={0}
                                        max={1}
                                        step={0.05}
                                        value={settings.indexRate}
                                        onChange={(e) =>
                                            handleVoiceParamChange(settings.pitchShift, Number(e.target.value), settings.protectRate)
                                        }
                                        style={{ flex: 1, cursor: 'pointer' }}
                                    />
                                </div>
                            </div>
                        </div>

                        {/* LIVE LOGS & STATUS */}
                        <div style={{ background: '#0f172a', padding: '16px', borderRadius: '16px', border: '1px solid #1e293b' }}>
                            <h3 style={{ margin: '0 0 8px 0', fontSize: '13px', color: '#94a3b8' }}>Engine Activity Logs</h3>
                            <div
                                style={{
                                    background: '#020617',
                                    padding: '10px',
                                    borderRadius: '8px',
                                    height: '85px',
                                    overflowY: 'auto',
                                    fontFamily: 'monospace',
                                    fontSize: '11px',
                                    color: '#38bdf8'
                                }}
                            >
                                {logs.map((l, i) => (
                                    <div key={i}>{l}</div>
                                ))}
                            </div>
                        </div>
                    </div>
                )}

                {/* ==================== TAB 2: VOICE PROFILES & LIBRARY ==================== */}
                {activeTab === 'voices' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid #1e293b', paddingBottom: '14px' }}>
                            <div>
                                <h1 style={{ margin: 0, fontSize: '22px', color: '#38bdf8' }}>🗣️ Voice Profiles & RVC Studio Library</h1>
                                <p style={{ margin: '4px 0 0', fontSize: '13px', color: '#94a3b8' }}>
                                    Upload pre-cloned .onnx RVC models or .wav/.mp3 studio voice samples for real-time 48kHz conversion.
                                </p>
                            </div>
                            <div style={{ display: 'flex', gap: '10px' }}>
                                <button
                                    onClick={async () => {
                                        if (!voicePreviewActive) {
                                            await EngineIPCClient.sendCommand({ command: 'START_VOICE_PREVIEW' });
                                            setVoicePreviewActive(true);
                                            addLog('🎧 Local microphone Voice Preview started.');
                                        } else {
                                            await EngineIPCClient.sendCommand({ command: 'STOP_VOICE_PREVIEW' });
                                            setVoicePreviewActive(false);
                                            addLog('🛑 Local microphone Voice Preview stopped.');
                                        }
                                    }}
                                    style={{
                                        background: voicePreviewActive ? '#dc2626' : '#1e293b',
                                        color: '#f8fafc',
                                        border: '1px solid #38bdf8',
                                        padding: '10px 16px',
                                        borderRadius: '8px',
                                        fontWeight: 600,
                                        cursor: 'pointer'
                                    }}
                                >
                                    {voicePreviewActive ? '🛑 Stop Mic Preview' : '🎧 Test Voice Preview'}
                                </button>
                                <button
                                    onClick={handleNativeVoicePick}
                                    style={{
                                        background: '#38bdf8',
                                        color: '#020617',
                                        border: 'none',
                                        padding: '10px 18px',
                                        borderRadius: '8px',
                                        fontWeight: 700,
                                        cursor: 'pointer'
                                    }}
                                >
                                    + Upload Voice (.wav / .mp3 / .onnx)
                                </button>
                            </div>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(280px, 1fr))', gap: '16px' }}>
                            {voices.map((v) => {
                                const isSelected = v.id === activeProfileId;
                                return (
                                    <div
                                        key={v.id}
                                        onClick={async () => {
                                            setActiveProfileId(v.id);
                                            await EngineIPCClient.setVoiceProfile(v.modelPath);
                                            addLog(`🗣️ Switched active voice profile to: ${v.displayName}`);
                                        }}
                                        style={{
                                            background: '#0f172a',
                                            padding: '18px',
                                            borderRadius: '12px',
                                            border: isSelected ? '2px solid #38bdf8' : '1px solid #1e293b',
                                            cursor: 'pointer'
                                        }}
                                    >
                                        <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '8px' }}>
                                            <strong style={{ color: '#f8fafc', fontSize: '15px' }}>{v.displayName}</strong>
                                            {isSelected && (
                                                <span style={{ background: '#0284c7', color: '#fff', padding: '2px 8px', borderRadius: '4px', fontSize: '11px' }}>
                                                    ACTIVE
                                                </span>
                                            )}
                                        </div>
                                        <p style={{ margin: '0 0 6px 0', fontSize: '11px', color: '#64748b', wordBreak: 'break-all' }}>
                                            Path: {v.modelPath}
                                        </p>
                                        <span style={{ fontSize: '11px', color: '#22c55e' }}>✓ 48kHz Studio Mono • Zero-Lag Ready</span>
                                    </div>
                                );
                            })}
                        </div>
                    </div>
                )}

                {/* ==================== TAB 3: DEVICE ROUTING (OBS / ZOOM / DISCORD) ==================== */}
                {activeTab === 'routing' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header style={{ borderBottom: '1px solid #1e293b', paddingBottom: '14px' }}>
                            <h1 style={{ margin: 0, fontSize: '22px', color: '#38bdf8' }}>🎛️ Virtual Device Routing (Zoom, OBS, Discord, Teams)</h1>
                            <p style={{ margin: '4px 0 0', fontSize: '13px', color: '#94a3b8' }}>
                                Real-time memory-mapped video & 48kHz virtual microphone bridge telemetry.
                            </p>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '14px', border: '1px solid #1e293b' }}>
                                <h3 style={{ margin: '0 0 10px 0', color: '#38bdf8', fontSize: '16px' }}>📹 Virtual Camera Shared Memory Bridge</h3>
                                <p style={{ fontSize: '13px', color: '#cbd5e1', margin: '0 0 8px 0' }}>
                                    Active Input Source: <strong>{activeEngineSource}</strong>
                                </p>
                                <p style={{ fontSize: '13px', color: '#cbd5e1', margin: '0 0 8px 0' }}>
                                    Output Resolution: <strong>1280x720 @ {liveFps > 0 ? `${liveFps.toFixed(1)} FPS` : '30 FPS Target'}</strong>
                                </p>
                                <p style={{ fontSize: '12px', color: '#22c55e', margin: 0 }}>
                                    ✓ Triple-Buffered Zero-Copy Shared Memory (/AIStudioVirtualCam & OBSVirtualCamVideo)
                                </p>
                            </div>

                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '14px', border: '1px solid #1e293b' }}>
                                <h3 style={{ margin: '0 0 10px 0', color: '#38bdf8', fontSize: '16px' }}>🎙️ Virtual Microphone Audio Bridge</h3>
                                <p style={{ fontSize: '13px', color: '#cbd5e1', margin: '0 0 8px 0' }}>
                                    Routed 10ms Audio Frames: <strong>{kinematics.routedFrames}</strong>
                                </p>
                                <p style={{ fontSize: '13px', color: '#cbd5e1', margin: '0 0 8px 0' }}>
                                    Live Speech Energy (Lip-Sync Driver): <strong>{Math.round(kinematics.speechEnergy * 100)}%</strong>
                                </p>
                                <p style={{ fontSize: '12px', color: '#22c55e', margin: 0 }}>
                                    ✓ Lock-Free SPSC Ring Buffer (Power-of-Two Bitwise Masked)
                                </p>
                            </div>
                        </div>
                    </div>
                )}

                {/* ==================== TAB 4: CLOUD & FIREBASE SYNC ==================== */}
                {activeTab === 'firebase' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid #1e293b', paddingBottom: '14px' }}>
                            <div>
                                <h1 style={{ margin: 0, fontSize: '22px', color: '#38bdf8' }}>☁️ Cloud & Local ONNX Model Cache</h1>
                                <p style={{ margin: '4px 0 0', fontSize: '13px', color: '#94a3b8' }}>
                                    Synchronize and verify local ONNX neural network weights in /models.
                                </p>
                            </div>
                            <button
                                onClick={async () => {
                                    const res = await EngineIPCClient.syncCloudModels();
                                    if (res.models) setCachedModels(res.models);
                                    addLog('☁️ Synchronized local ONNX model cache with C++ ModelManager.');
                                }}
                                style={{
                                    background: '#38bdf8',
                                    color: '#020617',
                                    border: 'none',
                                    padding: '10px 18px',
                                    borderRadius: '8px',
                                    fontWeight: 700,
                                    cursor: 'pointer'
                                }}
                            >
                                🔄 Verify & Sync Models
                            </button>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(260px, 1fr))', gap: '14px' }}>
                            {cachedModels.map((m) => (
                                <div key={m.id} style={{ background: '#0f172a', padding: '16px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                                    <strong style={{ color: '#38bdf8', fontSize: '14px' }}>{m.id}.onnx</strong>
                                    <p style={{ fontSize: '11px', color: '#94a3b8', margin: '6px 0' }}>{m.path}</p>
                                    <span style={{ fontSize: '11px', color: '#22c55e' }}>
                                        ✓ Verified ({(m.size_bytes / 1024).toFixed(1)} KB)
                                    </span>
                                </div>
                            ))}
                        </div>
                    </div>
                )}

                {/* ==================== TAB 5: HARDWARE & PERFORMANCE ==================== */}
                {activeTab === 'settings' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header style={{ borderBottom: '1px solid #1e293b', paddingBottom: '14px' }}>
                            <h1 style={{ margin: 0, fontSize: '22px', color: '#38bdf8' }}>⚙️ Hardware Acceleration & Full-Body Telemetry</h1>
                            <p style={{ margin: '4px 0 0', fontSize: '13px', color: '#94a3b8' }}>
                                Real-time C++20 hardware execution provider and 33-joint skeletal kinematics monitor.
                            </p>
                        </header>

                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '14px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', gap: '10px' }}>
                            <h3 style={{ margin: 0, color: '#e2e8f0', fontSize: '15px' }}>Real-Time Body & Hand Kinematics State</h3>
                            <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                • Left Hand Raised: <strong>{kinematics.leftArmRaised ? 'YES ✋' : 'No'}</strong> | Right Hand Raised: <strong>{kinematics.rightArmRaised ? 'YES ✋' : 'No'}</strong>
                            </p>
                            <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                • Hands Holding Body / Torso: <strong>{kinematics.handsHoldingBody ? 'DETECTED (Protected) 🤲' : 'Clear'}</strong>
                            </p>
                            <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                • Head Tilt Angle: <strong>{kinematics.headTilt.toFixed(1)}°</strong> | Framing: <strong>{kinematics.fullBodyVisible ? 'Full-Body / Torso Visible' : 'Close-Up Face'}</strong>
                            </p>
                        </div>
                    </div>
                )}
            </main>
        </div>
    );
}

export default App;