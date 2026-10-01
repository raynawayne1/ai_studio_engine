import { useState, useEffect, useRef, useCallback } from 'react';
import { Sidebar } from './components/Sidebar';
import { NavigationTab, VoiceProfile, EngineSettings } from './types';
import { EngineIPCClient, CachedModelItem } from './services/EngineIPCClient';

type CameraDevice = {
    id: string;
    label: string;
    index: number;
    endpoint: string;
    isStream: boolean;
    isVirtual?: boolean;
    isPhoneBridge?: boolean;
    webrtcDeviceId?: string;
};

type PhoneBridgeInfo = {
    phoneStreaming: boolean;
    framesReceived: number;
    usbUrl: string;
    mode: string;
    message: string;
    interfaces: Array<{
        iface: string;
        address: string;
        isUsbTether: boolean;
        httpUrl: string;
        httpsUrl: string;
    }>;
};

const BUILTIN_PHONE_BRIDGE_SOURCE: CameraDevice = {
    id: 'usb_phone_bridge',
    label: '📱 USB-C Phone Camera (Built-In Direct Bridge)',
    index: -1,
    endpoint: 'usb_phone_push',
    isStream: true,
    isPhoneBridge: true
};

export function App() {
    const [activeTab, setActiveTab] = useState<NavigationTab>('dashboard');
    const [engineRunning, setEngineRunning] = useState<boolean>(false);
    const [logs, setLogs] = useState<string[]>([]);
    const [showLogsDrawer, setShowLogsDrawer] = useState<boolean>(false);
    const [engineLoading, setEngineLoading] = useState<boolean>(false);

    // Viewport sizing: 'split' (large side-by-side), 'source_max', or 'target_max'
    const [viewportLayout, setViewportLayout] = useState<'split' | 'source_max' | 'target_max'>('split');

    const [cameras, setCameras] = useState<CameraDevice[]>([BUILTIN_PHONE_BRIDGE_SOURCE]);
    const [selectedCam, setSelectedCam] = useState<CameraDevice>(BUILTIN_PHONE_BRIDGE_SOURCE);
    const [usbScanning, setUsbScanning] = useState<boolean>(false);
    const [showPhoneHelper, setShowPhoneHelper] = useState<boolean>(false);
    const [phoneBridgeInfo, setPhoneBridgeInfo] = useState<PhoneBridgeInfo>({
        phoneStreaming: false,
        framesReceived: 0,
        usbUrl: 'http://127.0.0.1:8766/camera',
        mode: 'ready',
        message: 'Plug phone into USB-C and click Connect USB-C Phone',
        interfaces: []
    });

    const [liveFps, setLiveFps] = useState<number>(0);
    const [activeEngineSource, setActiveEngineSource] = useState<string>('Idle');

    // Live C++ JPEG ObjectURLs & Frame Counters
    const [cppSourceFrameUrl, setCppSourceFrameUrl] = useState<string>('');
    const [cppOutputFrameUrl, setCppOutputFrameUrl] = useState<string>('');
    const [previewTelemetry, setPreviewTelemetry] = useState({
        cppSourceFrames: 0,
        cppOutputFrames: 0,
        webrtcPushedFrames: 0,
        lastOutputBytes: 0
    });

    const prevSourceBlobUrlRef = useRef<string>('');
    const prevOutputBlobUrlRef = useRef<string>('');
    const sourceFrameCounterRef = useRef<number>(0);
    const outputFrameCounterRef = useRef<number>(0);
    const pushedFrameCounterRef = useRef<number>(0);
    const emptyFrameLoggedRef = useRef<boolean>(false);

    // Real-Time Body Kinematics & Audio Telemetry from C++ Engine
    const [kinematics, setKinematics] = useState({
        leftArmRaised: false,
        rightArmRaised: false,
        handsHoldingBody: false,
        fullBodyVisible: true,
        eyesBlinking: false,
        headTilt: 0,
        speechEnergy: 0,
        audioPeak: 0,
        inputGain: 1.0,
        routedFrames: 0,
        cloudStatus: 'UpToDate'
    });

    const [hardwareProfile, setHardwareProfile] = useState({
        osName: 'Detecting...',
        cpuArch: 'Detecting...',
        logicalCores: 0,
        totalRamGb: 0,
        provider: 'Apple CoreML / SIMD'
    });

    const [backgroundMode, setBackgroundMode] = useState<'disabled' | 'blur' | 'virtual' | 'transparent'>('disabled');
    const [voicePreviewActive, setVoicePreviewActive] = useState<boolean>(false);
    const [cachedModels, setCachedModels] = useState<CachedModelItem[]>([]);

    const sourceVideoRef = useRef<HTMLVideoElement>(null);
    const hiddenCaptureCanvasRef = useRef<HTMLCanvasElement>(null);
    const activeStreamRef = useRef<MediaStream | null>(null);
    const [webrtcPreviewActive, setWebrtcPreviewActive] = useState<boolean>(false);

    // Real Cloned Voices State (Loaded dynamically from C++ VoiceLibraryManager — NO FAKE VOICES)
    const [voices, setVoices] = useState<VoiceProfile[]>([]);
    const [activeProfileId, setActiveProfileId] = useState<string>('');
    const [activeVoiceDisplayName, setActiveVoiceDisplayName] = useState<string>('No Cloned Voice Yet');

    // Admin Voice Cloning Studio State (Tab 2)
    const [cloneNameInput, setCloneNameInput] = useState<string>('');
    const [cloneSourceFilePath, setCloneSourceFilePath] = useState<string>('');
    const [cloningBusy, setCloningBusy] = useState<boolean>(false);

    const [avatarImagePath, setAvatarImagePath] = useState<string>('');
    const [avatarPreviewUrl, setAvatarPreviewUrl] = useState<string>('');

    const [settings, setSettings] = useState<EngineSettings>({
        pitchShift: 0,
        indexRate: 0.88,
        protectRate: 0.33,
        inputGain: 1.0,
        faceSwapEnabled: true,
        lipSyncEnabled: true,
        backgroundMattingEnabled: false,
        garmentOverlayEnabled: false,
        performanceMode: 'Performance'
    });

    const addLog = useCallback((message: string) => {
        console.log(`[DEBUG][ui/src/App.tsx] ${message}`);
        setLogs((prev) => [`[${new Date().toLocaleTimeString()}] ${message}`, ...prev.slice(0, 99)]);
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
        setWebrtcPreviewActive(false);
    }, []);

    // Load Real Cloned Voices from C++ VoiceLibraryManager
    const refreshVoicesFromEngine = useCallback(async () => {
        const res = await EngineIPCClient.getVoices();
        if (res.status === 'ok' && Array.isArray(res.voices)) {
            setVoices(res.voices);
            if (res.active_voice) {
                setActiveProfileId(res.active_voice);
                const found = res.voices.find((v) => v.id === res.active_voice);
                setActiveVoiceDisplayName(found ? found.displayName : res.active_voice);
            } else if (res.voices.length > 0) {
                setActiveProfileId(res.voices[0].id);
                setActiveVoiceDisplayName(res.voices[0].displayName);
            } else {
                setActiveProfileId('');
                setActiveVoiceDisplayName('No Cloned Voice Yet');
            }
        }
    }, []);

    // Dynamic Discovery of ALL Internal, External USB, Virtual & USB-C Phone Cameras
    const scanCameras = useCallback(async (isManualUsbTrigger = false) => {
        try {
            setUsbScanning(true);
            addLog('[ui/src/App.tsx::scanCameras] Scanning Internal, External USB, Virtual & USB-C Phone cameras...');

            // @ts-ignore
            if (window.electronAPI?.bridgeUsbTypeC) {
                // @ts-ignore
                const bridgeRes = await window.electronAPI.bridgeUsbTypeC(isManualUsbTrigger).catch(() => null);
                if (bridgeRes) {
                    setPhoneBridgeInfo((prev) => ({
                        ...prev,
                        phoneStreaming: Boolean(bridgeRes.phoneActive),
                        usbUrl: bridgeRes.usbUrl || prev.usbUrl,
                        mode: bridgeRes.mode || 'ready',
                        message: bridgeRes.message || prev.message,
                        interfaces: Array.isArray(bridgeRes.interfaces) ? bridgeRes.interfaces : prev.interfaces
                    }));
                }
            }

            const discoveredMap = new Map<string, CameraDevice>();

            const cppScan = await EngineIPCClient.scanCameras();
            if (cppScan.status === 'ok' && Array.isArray(cppScan.devices)) {
                cppScan.devices.forEach((dev) => {
                    if (dev.is_stream) {
                        const isBuiltInBridge = dev.endpoint === 'usb_phone_push';
                        discoveredMap.set(isBuiltInBridge ? 'usb_phone_bridge' : `stream_${dev.endpoint}`, {
                            id: isBuiltInBridge ? 'usb_phone_bridge' : `stream_${dev.endpoint}`,
                            label: isBuiltInBridge
                                ? '📱 USB-C Phone Camera (Built-In Direct Bridge)'
                                : `🔌 ${dev.name}`,
                            index: -1,
                            endpoint: dev.endpoint,
                            isStream: true,
                            isPhoneBridge: isBuiltInBridge
                        });
                    } else {
                        const prefix = dev.is_virtual ? '🎥 Virtual: ' : '📷 Hardware: ';
                        discoveredMap.set(`hw_${dev.index}`, {
                            id: `hw_${dev.index}`,
                            label: `${prefix}[Index ${dev.index}] ${dev.name}`,
                            index: dev.index,
                            endpoint: String(dev.index),
                            isStream: false,
                            isVirtual: dev.is_virtual
                        });
                    }
                });
            }

            const mediaDevices = await navigator.mediaDevices.enumerateDevices().catch(() => []);
            const videoInputs = mediaDevices.filter((d) => d.kind === 'videoinput');

            videoInputs.forEach((dev, idx) => {
                const rawLabel = dev.label || `System / USB Camera #${idx}`;
                const lower = rawLabel.toLowerCase();
                const isVirtual =
                    lower.includes('virtual') ||
                    lower.includes('obs') ||
                    lower.includes('loopback');

                const existingHw = discoveredMap.get(`hw_${idx}`);
                if (existingHw) {
                    existingHw.label = `${isVirtual ? '🎥' : '📷'} [${idx}] ${rawLabel}`;
                    existingHw.webrtcDeviceId = dev.deviceId;
                    existingHw.isVirtual = isVirtual;
                } else {
                    discoveredMap.set(`hw_${idx}`, {
                        id: `hw_${idx}`,
                        label: `${isVirtual ? '🎥' : '📷'} [${idx}] ${rawLabel}`,
                        index: idx,
                        endpoint: String(idx),
                        isStream: false,
                        isVirtual,
                        webrtcDeviceId: dev.deviceId
                    });
                }
            });

            if (!discoveredMap.has('usb_phone_bridge')) {
                discoveredMap.set('usb_phone_bridge', BUILTIN_PHONE_BRIDGE_SOURCE);
            }

            const finalList = Array.from(discoveredMap.values());
            setCameras((prev) => {
                const customFiles = prev.filter((c) => c.id.startsWith('file_'));
                return [...customFiles, ...finalList];
            });

            setSelectedCam((prev) => {
                if (isManualUsbTrigger) {
                    return discoveredMap.get('usb_phone_bridge') || BUILTIN_PHONE_BRIDGE_SOURCE;
                }
                const match = finalList.find((c) => c.id === prev.id);
                if (match) return match;
                const workingCam = finalList.find((c) => c.index >= 0 && !c.isVirtual);
                return workingCam || discoveredMap.get('usb_phone_bridge') || finalList[0];
            });

            addLog(
                `[ui/src/App.tsx::scanCameras] Discovery Complete: ${finalList.filter((c) => c.index >= 0).length} OS Camera(s) + Built-In USB-C Phone Bridge Ready.`
            );
        } catch (err: any) {
            addLog(`❌ [ui/src/App.tsx::scanCameras] Error: ${err.message}`);
        } finally {
            setUsbScanning(false);
        }
    }, [addLog]);

    useEffect(() => {
        scanCameras(false);
        refreshVoicesFromEngine();
        EngineIPCClient.syncCloudModels().then((res) => {
            if (res.models) setCachedModels(res.models);
        });
        EngineIPCClient.getHardwareProfile().then((res) => {
            if (res.status === 'ok') {
                setHardwareProfile({
                    osName: res.os_name || 'macOS / POSIX',
                    cpuArch: res.cpu_arch || 'ARM64 / x86_64',
                    logicalCores: res.logical_cores || 8,
                    totalRamGb: res.total_ram_gb || 16,
                    provider: res.provider || 'Apple CoreML / SIMD'
                });
            }
        });
        if (navigator.mediaDevices?.addEventListener) {
            const handleDeviceChange = () => scanCameras(false);
            navigator.mediaDevices.addEventListener('devicechange', handleDeviceChange);
            return () => navigator.mediaDevices.removeEventListener('devicechange', handleDeviceChange);
        }
    }, [scanCameras, refreshVoicesFromEngine]);

    // Real-Time Microphone Capture -> C++ VoiceInferenceEngine Conversion -> Live Audio Output
    useEffect(() => {
        if (!engineRunning && !voicePreviewActive) {
            return;
        }

        let audioCtx: AudioContext | null = null;
        let micStream: MediaStream | null = null;
        let scriptNode: ScriptProcessorNode | null = null;
        let sourceNode: MediaStreamAudioSourceNode | null = null;
        let active = true;
        let busyAudio = false;
        let latestConvertedBuffer: Float32Array | null = null;

        const startLiveMicBridge = async () => {
            try {
                micStream = await navigator.mediaDevices.getUserMedia({
                    audio: {
                        echoCancellation: true,
                        noiseSuppression: true,
                        autoGainControl: false
                    },
                    video: false
                });
                if (!active) {
                    micStream.getTracks().forEach((t) => t.stop());
                    return;
                }

                audioCtx = new AudioContext({ sampleRate: 48000 });
                sourceNode = audioCtx.createMediaStreamSource(micStream);
                scriptNode = audioCtx.createScriptProcessor(2048, 1, 1);

                scriptNode.onaudioprocess = (audioEvent) => {
                    if (!active) return;
                    const inputData = audioEvent.inputBuffer.getChannelData(0);
                    const outputData = audioEvent.outputBuffer.getChannelData(0);

                    // Output most recently converted C++ cloned voice buffer when previewing
                    if (voicePreviewActive && latestConvertedBuffer && latestConvertedBuffer.length === outputData.length) {
                        outputData.set(latestConvertedBuffer);
                    } else {
                        outputData.fill(0);
                    }

                    if (!busyAudio) {
                        busyAudio = true;
                        const copyIn = new Float32Array(inputData);
                        EngineIPCClient.processLiveMicAudio(copyIn)
                            .then((converted) => {
                                if (converted && converted.length > 0) {
                                    latestConvertedBuffer = converted;
                                }
                            })
                            .finally(() => {
                                busyAudio = false;
                            });
                    }
                };

                sourceNode.connect(scriptNode);
                scriptNode.connect(audioCtx.destination);
                addLog('🎙️ [ui/src/App.tsx::LiveMicBridge] Real 48kHz microphone stream connected to C++ VoiceInferenceEngine.');
            } catch (err: any) {
                addLog(`⚠️ [ui/src/App.tsx::LiveMicBridge] Mic capture notice: ${err.message}`);
            }
        };

        startLiveMicBridge();

        return () => {
            active = false;
            if (scriptNode) scriptNode.disconnect();
            if (sourceNode) sourceNode.disconnect();
            if (micStream) micStream.getTracks().forEach((t) => t.stop());
            if (audioCtx) audioCtx.close().catch(() => null);
        };
    }, [engineRunning, voicePreviewActive, addLog]);

    // Poll C++ Engine Status + Built-In USB-C Phone Bridge Status
    useEffect(() => {
        let prevPhoneState = false;
        const timer = setInterval(async () => {
            const status = await EngineIPCClient.getCameraStatus();
            if (status.status === 'ok') {
                if (typeof status.fps === 'number') setLiveFps(status.fps);
                if (status.active_source) setActiveEngineSource(status.active_source);
                if (status.active_voice_name) setActiveVoiceDisplayName(status.active_voice_name);
                setKinematics({
                    leftArmRaised: Boolean(status.left_arm_raised),
                    rightArmRaised: Boolean(status.right_arm_raised),
                    handsHoldingBody: Boolean(status.hands_holding_body),
                    fullBodyVisible: status.full_body_visible ?? true,
                    eyesBlinking: Boolean(status.eyes_blinking),
                    headTilt: status.head_tilt ?? 0,
                    speechEnergy: status.speech_energy ?? 0,
                    audioPeak: status.audio_peak ?? 0,
                    inputGain: status.input_gain ?? 1.0,
                    routedFrames: status.routed_frames ?? 0,
                    cloudStatus: status.cloud_status || 'UpToDate'
                });
            }

            // @ts-ignore
            if (window.electronAPI?.getPhoneBridgeStatus) {
                // @ts-ignore
                const pStatus = await window.electronAPI.getPhoneBridgeStatus().catch(() => null);
                if (pStatus) {
                    const isNowStreaming = Boolean(pStatus.phoneStreaming);
                    if (isNowStreaming && !prevPhoneState) {
                        addLog(
                            `📱 [ui/src/App.tsx::PhoneBridge] Live USB-C Phone Camera stream detected (${pStatus.framesReceived} frames received)!`
                        );
                    }
                    prevPhoneState = isNowStreaming;
                    setPhoneBridgeInfo((prev) => ({
                        ...prev,
                        phoneStreaming: isNowStreaming,
                        framesReceived: pStatus.framesReceived || 0,
                        interfaces: Array.isArray(pStatus.interfaces) ? pStatus.interfaces : prev.interfaces
                    }));
                }
            }
        }, 450);
        return () => clearInterval(timer);
    }, [addLog]);

    // Live Hardware Preview & WebRTC-to-C++ Frame Bridge
    useEffect(() => {
        let cancelled = false;

        const setupSourcePreview = async () => {
            releasePreviewStream();

            if (!selectedCam || selectedCam.isPhoneBridge || selectedCam.index < 0) {
                return;
            }

            try {
                const constraints: MediaStreamConstraints = {
                    audio: false,
                    video: selectedCam.webrtcDeviceId
                        ? {
                            deviceId: { exact: selectedCam.webrtcDeviceId },
                            width: { ideal: 1280 },
                            height: { ideal: 720 },
                            frameRate: { ideal: 30 }
                        }
                        : { width: { ideal: 1280 }, height: { ideal: 720 }, frameRate: { ideal: 30 } }
                };

                const stream = await navigator.mediaDevices.getUserMedia(constraints);
                if (cancelled) {
                    stream.getTracks().forEach((t) => t.stop());
                    return;
                }

                activeStreamRef.current = stream;
                if (sourceVideoRef.current) {
                    sourceVideoRef.current.srcObject = stream;
                    setWebrtcPreviewActive(true);
                    addLog(`✅ [ui/src/App.tsx::LivePreview] WebRTC Hardware Preview Active: ${selectedCam.label}`);
                }
            } catch (err: any) {
                setWebrtcPreviewActive(false);
                addLog(`⚠️ [ui/src/App.tsx::LivePreview] OS Camera "${selectedCam.label}" using C++ direct capture (${err.message}).`);
            }
        };

        setupSourcePreview();
        return () => {
            cancelled = true;
        };
    }, [selectedCam, releasePreviewStream, addLog]);

    // Zero-Lag Non-Overlapping C++ Live Preview Loop
    useEffect(() => {
        if (!engineRunning && !phoneBridgeInfo.phoneStreaming) {
            return;
        }

        let active = true;
        let inFlight = false;
        emptyFrameLoggedRef.current = false;

        const tick = async () => {
            if (!active || inFlight) return;
            inFlight = true;

            try {
                if (
                    engineRunning &&
                    webrtcPreviewActive &&
                    sourceVideoRef.current &&
                    sourceVideoRef.current.readyState >= 2 &&
                    hiddenCaptureCanvasRef.current
                ) {
                    const canvas = hiddenCaptureCanvasRef.current;
                    const ctx = canvas.getContext('2d');
                    if (ctx) {
                        ctx.drawImage(sourceVideoRef.current, 0, 0, canvas.width, canvas.height);
                        const blob = await new Promise<Blob | null>((resolve) =>
                            canvas.toBlob((b) => resolve(b), 'image/jpeg', 0.86)
                        );
                        if (blob) {
                            const pushed = await EngineIPCClient.pushRawFrameToEngine(blob);
                            if (pushed) {
                                pushedFrameCounterRef.current += 1;
                            }
                        }
                    }
                }

                if (!webrtcPreviewActive) {
                    const srcRes = await EngineIPCClient.fetchLiveFrame('source');
                    if (srcRes.ok && srcRes.objectUrl) {
                        if (prevSourceBlobUrlRef.current) {
                            URL.revokeObjectURL(prevSourceBlobUrlRef.current);
                        }
                        prevSourceBlobUrlRef.current = srcRes.objectUrl;
                        setCppSourceFrameUrl(srcRes.objectUrl);
                        sourceFrameCounterRef.current += 1;
                    }
                }

                if (engineRunning) {
                    const outRes = await EngineIPCClient.fetchLiveFrame('output');
                    if (outRes.ok && outRes.objectUrl) {
                        if (prevOutputBlobUrlRef.current) {
                            URL.revokeObjectURL(prevOutputBlobUrlRef.current);
                        }
                        prevOutputBlobUrlRef.current = outRes.objectUrl;
                        setCppOutputFrameUrl(outRes.objectUrl);
                        outputFrameCounterRef.current += 1;

                        setPreviewTelemetry({
                            cppSourceFrames: sourceFrameCounterRef.current,
                            cppOutputFrames: outputFrameCounterRef.current,
                            webrtcPushedFrames: pushedFrameCounterRef.current,
                            lastOutputBytes: outRes.bytes
                        });
                    }
                }
            } catch {
                // Ignore transient frame poll drops
            } finally {
                inFlight = false;
            }
        };

        const intervalId = setInterval(tick, 30);
        return () => {
            active = false;
            clearInterval(intervalId);
        };
    }, [engineRunning, phoneBridgeInfo.phoneStreaming, webrtcPreviewActive]);

    // Handlers
    const handleConnectUsbPhoneButton = async () => {
        setShowPhoneHelper(true);
        await scanCameras(true);
        const res = await EngineIPCClient.connectUsbOrStream('usb_phone_push', -1);
        addLog(`[ui/src/App.tsx::handleConnectUsbPhoneButton] ${res.message || res.status}`);
    };

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
                addLog(`👤 [ui/src/App.tsx] Target face picture sent to C++ FaceSwapEngine: ${selectedPath}`);
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
                addLog(`🖼️ [ui/src/App.tsx] Custom Virtual Background loaded into C++: ${selectedPath}`);
            }
        }
    };

    const handleBackgroundModeChange = async (mode: 'disabled' | 'blur' | 'virtual' | 'transparent') => {
        setBackgroundMode(mode);
        const enabled = mode !== 'disabled';
        setSettings((s) => ({ ...s, backgroundMattingEnabled: enabled }));
        await EngineIPCClient.toggleBackgroundMatting(enabled, mode);
        addLog(`🎬 [ui/src/App.tsx] C++ BackgroundMattingEngine mode=${mode.toUpperCase()}`);
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
            addLog(
                `🎭 [ui/src/App.tsx] C++ Vision Config -> face_swap=${next.faceSwapEnabled}, lip_sync=${next.lipSyncEnabled}, garment=${next.garmentOverlayEnabled}`
            );
            return next;
        });
    };

    const handleVoiceSelectChange = async (profileId: string) => {
        if (!profileId) return;
        setActiveProfileId(profileId);
        const found = voices.find((v) => v.id === profileId);
        if (found) {
            setActiveVoiceDisplayName(found.displayName);
        }
        await EngineIPCClient.setVoiceProfile(profileId);
        addLog(`🎙️ [ui/src/App.tsx] Active Cloned Voice switched to: ${found?.displayName || profileId}`);
    };

    const handleVoiceParamChange = (pitch: number, indexRate: number, protectRate: number, inputGain = settings.inputGain || 1.0) => {
        setSettings((prev) => ({
            ...prev,
            pitchShift: pitch,
            indexRate,
            protectRate,
            inputGain
        }));
        EngineIPCClient.sendAsyncCommand({
            command: 'SET_VOICE_PARAMS',
            pitch,
            index_rate: indexRate,
            protect_rate: protectRate,
            input_gain: inputGain
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
                    label: `🎬 Video File: ${fileName}`,
                    index: -1,
                    endpoint: selectedPath,
                    isStream: true
                };
                setCameras((prev) => [fileCam, ...prev]);
                setSelectedCam(fileCam);
                addLog(`🎬 [ui/src/App.tsx] Selected video file for C++ VirtualCameraManager: ${selectedPath}`);
                await EngineIPCClient.connectUsbOrStream(selectedPath, -1);
            }
        }
    };

    // Admin Voice Cloning Studio File Picker (Supports Video OR Audio files)
    const handlePickMediaForVoiceClone = async () => {
        // @ts-ignore
        if (window.electronAPI?.openFileDialog) {
            // @ts-ignore
            const selectedPath = await window.electronAPI.openFileDialog({
                filters: [
                    {
                        name: 'Video or Audio for Voice Cloning (.mp4, .mov, .wav, .mp3, .flac, .m4a, .onnx)',
                        extensions: ['mp4', 'mov', 'mkv', 'webm', 'wav', 'mp3', 'flac', 'm4a', 'ogg', 'onnx']
                    }
                ]
            });
            if (selectedPath) {
                setCloneSourceFilePath(selectedPath);
                if (!cloneNameInput.trim()) {
                    const base = selectedPath.split(/[\\/]/).pop() || 'My Cloned Voice';
                    setCloneNameInput(base.replace(/\.[^/.]+$/, ''));
                }
                addLog(`📂 [ui/src/App.tsx::VoiceClone] Selected source media for cloning: ${selectedPath}`);
            }
        }
    };

    const handleExecuteVoiceCloneAndSave = async () => {
        if (!cloneSourceFilePath) {
            return alert('Please select a Video (.mp4/.mov) or Audio (.wav/.mp3) file to clone first!');
        }
        const finalName = cloneNameInput.trim() || 'Custom Cloned Voice';
        setCloningBusy(true);
        addLog(`⚡ [ui/src/App.tsx::VoiceClone] Cloning voice "${finalName}" from ${cloneSourceFilePath}...`);
        try {
            const res = await EngineIPCClient.cloneVoiceFromFile(cloneSourceFilePath, finalName);
            if (res.status === 'ok' && Array.isArray(res.voices)) {
                setVoices(res.voices);
                if (res.active_voice) {
                    setActiveProfileId(res.active_voice);
                    setActiveVoiceDisplayName(finalName);
                }
                setCloneNameInput('');
                setCloneSourceFilePath('');
                addLog(`✅ [ui/src/App.tsx::VoiceClone] Voice "${finalName}" cloned, saved to /models, and activated for live calls!`);
            } else {
                alert(`Voice cloning failed: ${res.message || 'Invalid file'}`);
            }
        } finally {
            setCloningBusy(false);
        }
    };

    const handleDeleteClonedVoice = async (profileId: string, displayName: string) => {
        const res = await EngineIPCClient.deleteVoiceProfile(profileId);
        if (res.status === 'ok' && Array.isArray(res.voices)) {
            setVoices(res.voices);
            setActiveProfileId(res.active_voice || '');
            addLog(`🗑️ [ui/src/App.tsx] Deleted voice profile: ${displayName}`);
        }
    };

    const handleImportCustomOnnxModel = async () => {
        // @ts-ignore
        if (window.electronAPI?.openFileDialog) {
            // @ts-ignore
            const selectedPath = await window.electronAPI.openFileDialog({
                filters: [{ name: 'ONNX Neural Network Models (.onnx)', extensions: ['onnx'] }]
            });
            if (selectedPath) {
                const res = await EngineIPCClient.registerModel(selectedPath);
                if (res.models) {
                    setCachedModels(res.models);
                    addLog(`☁️ [ui/src/App.tsx] Registered ONNX model into /models: ${selectedPath}`);
                }
            }
        }
    };

    const handleToggleVoicePreview = async () => {
        if (!voicePreviewActive) {
            const res = await EngineIPCClient.sendCommand({ command: 'START_VOICE_PREVIEW' });
            if (res.status === 'ok') {
                setVoicePreviewActive(true);
                addLog(`🎧 [ui/src/App.tsx] Live Mic Cloned Voice Preview ON (${activeVoiceDisplayName}). Speak into your mic!`);
            }
        } else {
            await EngineIPCClient.sendCommand({ command: 'STOP_VOICE_PREVIEW' });
            setVoicePreviewActive(false);
            addLog('🛑 [ui/src/App.tsx] Live Mic Cloned Voice Preview stopped.');
        }
    };

    const handleToggleEngine = async () => {
        if (!engineRunning) {
            if (!avatarImagePath) {
                return alert('Please click "Choose Target Picture / Avatar" under 2. Target (AI Output) first!');
            }

            setEngineLoading(true);
            sourceFrameCounterRef.current = 0;
            outputFrameCounterRef.current = 0;
            pushedFrameCounterRef.current = 0;

            addLog(`🚀 [ui/src/App.tsx::handleToggleEngine] Starting C++20 Deep-Live-Cam Pipeline on source: ${selectedCam.label}...`);

            try {
                await EngineIPCClient.setFaceSwapAvatar(avatarImagePath);
                if (activeProfileId) {
                    await EngineIPCClient.setVoiceProfile(activeProfileId);
                }

                let camEndpoint = selectedCam.isStream ? selectedCam.endpoint : '';
                let camIndex = selectedCam.isStream ? -1 : selectedCam.index;

                if (webrtcPreviewActive && selectedCam.index >= 0) {
                    camEndpoint = 'usb_phone_push';
                    camIndex = -1;
                }

                const camRes = await EngineIPCClient.startCamera(camIndex, camEndpoint);
                await EngineIPCClient.sendCommand({
                    command: 'START_CALL',
                    profile: activeProfileId,
                    pitch: settings.pitchShift
                });

                if (camRes.status === 'ok') {
                    setEngineRunning(true);
                    setActiveEngineSource(camRes.active_source || selectedCam.label);
                    addLog(`✅ [ui/src/App.tsx] C++ Engine LIVE on [${camRes.active_source || selectedCam.label}]!`);
                    if (selectedCam.isPhoneBridge && !phoneBridgeInfo.phoneStreaming) {
                        setShowPhoneHelper(true);
                    }
                } else {
                    addLog(`❌ [ui/src/App.tsx] C++ Camera failed: ${camRes.message || 'Unknown error'}`);
                    alert(`Could not start video source:\n${camRes.message}`);
                }
            } catch (err: any) {
                addLog(`❌ [ui/src/App.tsx::handleToggleEngine] Exception: ${err.message}`);
            } finally {
                setEngineLoading(false);
            }
        } else {
            addLog('🛑 [ui/src/App.tsx] Stopping C++ AI Studio Pipeline...');
            await EngineIPCClient.stopCamera();
            await EngineIPCClient.sendCommand({ command: 'STOP_CALL' });
            setEngineRunning(false);
            setEngineLoading(false);
            setLiveFps(0);
            setCppOutputFrameUrl('');
            addLog('✅ [ui/src/App.tsx] C++ Pipeline stopped cleanly.');
        }
    };

    const monitorHeight = viewportLayout === 'split' ? 'calc(100vh - 305px)' : 'calc(100vh - 210px)';

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
            <canvas ref={hiddenCaptureCanvasRef} width={640} height={360} style={{ display: 'none' }} />

            <Sidebar
                activeTab={activeTab}
                setActiveTab={setActiveTab}
                callActive={engineRunning}
                activeSource={activeEngineSource}
                activeVoiceName={activeVoiceDisplayName}
                fps={liveFps}
            />

            <main style={{ flex: 1, display: 'flex', flexDirection: 'column', height: '100vh', overflowY: 'auto', padding: '14px 20px' }}>
                {/* ==================== TAB 1: LIVE STUDIO CALL DASHBOARD (LARGE HD MONITORS) ==================== */}
                {activeTab === 'dashboard' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '12px', flex: 1 }}>
                        <header
                            style={{
                                borderBottom: '1px solid #1e293b',
                                paddingBottom: '10px',
                                display: 'flex',
                                justifyContent: 'space-between',
                                alignItems: 'center'
                            }}
                        >
                            <div>
                                <h1 style={{ margin: 0, fontSize: '20px', color: '#38bdf8', fontWeight: 800 }}>
                                    AI Studio DeepLive Suite
                                </h1>
                                <p style={{ margin: '2px 0 0', color: '#94a3b8', fontSize: '11px' }}>
                                    Zero-Lag C++20 FaceSwapEngine • 33-Joint Body & Hand Tracker • LAB Skin Blending • Real Cloned Voice RVC
                                </p>
                            </div>

                            <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                                <button
                                    onClick={() => setShowLogsDrawer((s) => !s)}
                                    style={{
                                        background: showLogsDrawer ? '#38bdf8' : '#0f172a',
                                        color: showLogsDrawer ? '#020617' : '#cbd5e1',
                                        border: '1px solid #334155',
                                        padding: '9px 12px',
                                        borderRadius: '8px',
                                        fontSize: '12px',
                                        fontWeight: 600,
                                        cursor: 'pointer'
                                    }}
                                >
                                    📋 Debug Logs ({logs.length})
                                </button>
                                <button
                                    onClick={handleToggleEngine}
                                    disabled={engineLoading}
                                    style={{
                                        background: engineRunning ? '#dc2626' : '#16a34a',
                                        color: '#fff',
                                        border: 'none',
                                        padding: '10px 22px',
                                        borderRadius: '8px',
                                        fontWeight: 800,
                                        fontSize: '13px',
                                        cursor: engineLoading ? 'wait' : 'pointer'
                                    }}
                                >
                                    {engineLoading ? '⌛ Starting C++ Engine...' : engineRunning ? '🛑 STOP ENGINE' : '▶ START ENGINE (GO LIVE)'}
                                </button>
                            </div>
                        </header>

                        {/* BUILT-IN USB-C PHONE CAMERA CONNECTION HELPER */}
                        {showPhoneHelper && (
                            <div
                                style={{
                                    background: '#0f172a',
                                    border: '1px solid #38bdf8',
                                    borderRadius: '10px',
                                    padding: '10px 14px',
                                    display: 'flex',
                                    justifyContent: 'space-between',
                                    alignItems: 'center',
                                    gap: '12px'
                                }}
                            >
                                <div style={{ display: 'flex', flexDirection: 'column', gap: '3px' }}>
                                    <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                                        <strong style={{ color: '#38bdf8', fontSize: '12px' }}>
                                            📱 Built-In USB-C Phone Camera Bridge (Zero 3rd-Party Apps)
                                        </strong>
                                        <span
                                            style={{
                                                background: phoneBridgeInfo.phoneStreaming ? '#16a34a' : '#eab308',
                                                color: '#020617',
                                                padding: '2px 7px',
                                                borderRadius: '4px',
                                                fontSize: '10px',
                                                fontWeight: 800
                                            }}
                                        >
                                            {phoneBridgeInfo.phoneStreaming
                                                ? `STREAMING LIVE (${phoneBridgeInfo.framesReceived} frames)`
                                                : 'WAITING FOR PHONE'}
                                        </span>
                                    </div>
                                    <span style={{ fontSize: '11px', color: '#cbd5e1' }}>
                                        Plug phone into USB-C (enable <strong>USB Tethering</strong> or <strong>USB Debugging</strong>) and open:{' '}
                                        <strong style={{ color: '#22c55e', userSelect: 'all' }}>{phoneBridgeInfo.usbUrl}</strong>
                                        {phoneBridgeInfo.interfaces.length > 0 &&
                                            ` (or ${phoneBridgeInfo.interfaces.map((i) => i.httpsUrl).join(' / ')})`}
                                    </span>
                                </div>
                                <button
                                    onClick={() => setShowPhoneHelper(false)}
                                    style={{
                                        background: '#1e293b',
                                        color: '#cbd5e1',
                                        border: '1px solid #334155',
                                        borderRadius: '6px',
                                        padding: '5px 10px',
                                        fontSize: '11px',
                                        cursor: 'pointer'
                                    }}
                                >
                                    Hide
                                </button>
                            </div>
                        )}

                        {/* EXPANSIVE STUDIO MONITORS (LARGE VIEWPORTS + THEATER MODE) */}
                        <div
                            style={{
                                display: 'grid',
                                gridTemplateColumns:
                                    viewportLayout === 'split'
                                        ? '1fr 1fr'
                                        : '1fr',
                                gap: '16px',
                                flex: 1
                            }}
                        >
                            {/* 1. SOURCE MONITOR */}
                            {viewportLayout !== 'target_max' && (
                                <div
                                    style={{
                                        background: '#0f172a',
                                        padding: '12px',
                                        borderRadius: '14px',
                                        border: '1px solid #1e293b',
                                        display: 'flex',
                                        flexDirection: 'column',
                                        gap: '8px'
                                    }}
                                >
                                    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: '6px', flexWrap: 'wrap' }}>
                                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                                            <h3 style={{ margin: 0, fontSize: '14px', color: '#e2e8f0' }}>1. Source (You)</h3>
                                            <button
                                                onClick={() =>
                                                    setViewportLayout((v) => (v === 'source_max' ? 'split' : 'source_max'))
                                                }
                                                style={{
                                                    background: '#1e293b',
                                                    color: '#38bdf8',
                                                    border: '1px solid #334155',
                                                    borderRadius: '5px',
                                                    padding: '3px 8px',
                                                    fontSize: '11px',
                                                    cursor: 'pointer'
                                                }}
                                            >
                                                {viewportLayout === 'source_max' ? '🗗 Split View' : '⛶ Expand'}
                                            </button>
                                        </div>

                                        <div style={{ display: 'flex', gap: '5px', alignItems: 'center' }}>
                                            <select
                                                value={selectedCam?.id || ''}
                                                onChange={async (e) => {
                                                    const cam = cameras.find((c) => c.id === e.target.value);
                                                    if (cam) {
                                                        setSelectedCam(cam);
                                                        addLog(`📷 [ui/src/App.tsx] Switched active input source to: ${cam.label}`);
                                                        if (engineRunning) {
                                                            await EngineIPCClient.connectUsbOrStream(
                                                                cam.isStream ? cam.endpoint : '',
                                                                cam.isStream ? -1 : cam.index
                                                            );
                                                        }
                                                    }
                                                }}
                                                style={{
                                                    padding: '5px 8px',
                                                    background: '#020617',
                                                    border: '1px solid #334155',
                                                    color: '#38bdf8',
                                                    borderRadius: '6px',
                                                    fontSize: '12px',
                                                    maxWidth: '240px'
                                                }}
                                            >
                                                {cameras.map((cam) => (
                                                    <option key={cam.id} value={cam.id}>
                                                        {cam.label}
                                                    </option>
                                                ))}
                                            </select>
                                            <button
                                                onClick={() => scanCameras(false)}
                                                disabled={usbScanning}
                                                title="Rescan all cameras"
                                                style={{
                                                    background: '#1e293b',
                                                    color: '#38bdf8',
                                                    border: '1px solid #334155',
                                                    borderRadius: '6px',
                                                    padding: '5px 8px',
                                                    fontSize: '12px',
                                                    cursor: 'pointer'
                                                }}
                                            >
                                                🔄
                                            </button>
                                            <button
                                                onClick={handleConnectUsbPhoneButton}
                                                style={{
                                                    background: '#0284c7',
                                                    color: '#fff',
                                                    border: 'none',
                                                    borderRadius: '6px',
                                                    padding: '6px 10px',
                                                    fontSize: '11px',
                                                    fontWeight: 700,
                                                    cursor: 'pointer'
                                                }}
                                            >
                                                📱 USB-C Phone
                                            </button>
                                            <button
                                                onClick={handleNativeVideoSourcePick}
                                                style={{
                                                    background: '#1e293b',
                                                    color: '#e2e8f0',
                                                    border: '1px solid #334155',
                                                    borderRadius: '6px',
                                                    padding: '6px 10px',
                                                    fontSize: '11px',
                                                    fontWeight: 600,
                                                    cursor: 'pointer'
                                                }}
                                            >
                                                🎬 Video File
                                            </button>
                                        </div>
                                    </div>

                                    {/* LARGE HD SOURCE MONITOR */}
                                    <div
                                        style={{
                                            background: '#020617',
                                            borderRadius: '10px',
                                            border: '1px solid #1e293b',
                                            height: monitorHeight,
                                            minHeight: '460px',
                                            overflow: 'hidden',
                                            position: 'relative',
                                            display: 'flex',
                                            alignItems: 'center',
                                            justifyContent: 'center'
                                        }}
                                    >
                                        <video
                                            ref={sourceVideoRef}
                                            autoPlay
                                            playsInline
                                            muted
                                            style={{
                                                width: '100%',
                                                height: '100%',
                                                objectFit: 'contain',
                                                background: '#000',
                                                transform: 'scaleX(-1)',
                                                display: webrtcPreviewActive ? 'block' : 'none'
                                            }}
                                        />

                                        {!webrtcPreviewActive && cppSourceFrameUrl && (engineRunning || phoneBridgeInfo.phoneStreaming) ? (
                                            <img
                                                src={cppSourceFrameUrl}
                                                alt="C++ Live Source Feed"
                                                style={{ width: '100%', height: '100%', objectFit: 'contain', background: '#000' }}
                                            />
                                        ) : (
                                            !webrtcPreviewActive && (
                                                <div style={{ textAlign: 'center', padding: '24px', color: '#64748b' }}>
                                                    <div style={{ fontSize: '38px', marginBottom: '8px' }}>📷</div>
                                                    <div style={{ fontSize: '14px', color: '#cbd5e1', fontWeight: 700 }}>
                                                        {selectedCam.label}
                                                    </div>
                                                    <div style={{ fontSize: '12px', color: '#94a3b8', marginTop: '6px' }}>
                                                        Select a camera from the dropdown above, click <strong>"📱 USB-C Phone"</strong>, or load a video file.
                                                    </div>
                                                </div>
                                            )
                                        )}

                                        <div
                                            style={{
                                                position: 'absolute',
                                                bottom: '10px',
                                                left: '10px',
                                                background: 'rgba(2,6,23,0.88)',
                                                padding: '4px 10px',
                                                borderRadius: '6px',
                                                fontSize: '11px',
                                                color: '#38bdf8',
                                                border: '1px solid #1e293b'
                                            }}
                                        >
                                            🔴 {selectedCam.label} •{' '}
                                            {webrtcPreviewActive
                                                ? `WebRTC Frame #${previewTelemetry.webrtcPushedFrames}`
                                                : `CPP-SRC #${previewTelemetry.cppSourceFrames}`}
                                        </div>
                                    </div>
                                </div>
                            )}

                            {/* 2. TARGET AI OUTPUT MONITOR */}
                            {viewportLayout !== 'source_max' && (
                                <div
                                    style={{
                                        background: '#0f172a',
                                        padding: '12px',
                                        borderRadius: '14px',
                                        border: '1px solid #38bdf8',
                                        display: 'flex',
                                        flexDirection: 'column',
                                        gap: '8px'
                                    }}
                                >
                                    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                                            <h3 style={{ margin: 0, fontSize: '14px', color: '#e2e8f0' }}>
                                                2. Target (Live C++ AI Face Swap & Full-Body Output)
                                            </h3>
                                            <button
                                                onClick={() =>
                                                    setViewportLayout((v) => (v === 'target_max' ? 'split' : 'target_max'))
                                                }
                                                style={{
                                                    background: '#1e293b',
                                                    color: '#38bdf8',
                                                    border: '1px solid #334155',
                                                    borderRadius: '5px',
                                                    padding: '3px 8px',
                                                    fontSize: '11px',
                                                    cursor: 'pointer'
                                                }}
                                            >
                                                {viewportLayout === 'target_max' ? '🗗 Split View' : '⛶ Expand'}
                                            </button>
                                        </div>

                                        <button
                                            onClick={handleNativeAvatarPick}
                                            style={{
                                                background: '#38bdf8',
                                                color: '#020617',
                                                padding: '6px 14px',
                                                borderRadius: '6px',
                                                fontSize: '12px',
                                                fontWeight: 800,
                                                cursor: 'pointer',
                                                border: 'none'
                                            }}
                                        >
                                            🖼️ Choose Target Picture / Avatar
                                        </button>
                                    </div>

                                    {/* LARGE HD TARGET MONITOR */}
                                    <div
                                        style={{
                                            background: '#020617',
                                            borderRadius: '10px',
                                            border: '1px solid #38bdf8',
                                            height: monitorHeight,
                                            minHeight: '460px',
                                            overflow: 'hidden',
                                            position: 'relative',
                                            display: 'flex',
                                            alignItems: 'center',
                                            justifyContent: 'center'
                                        }}
                                    >
                                        {engineRunning && cppOutputFrameUrl ? (
                                            <img
                                                src={cppOutputFrameUrl}
                                                alt="C++ AI Output Stream"
                                                style={{ width: '100%', height: '100%', objectFit: 'contain', background: '#000' }}
                                            />
                                        ) : avatarPreviewUrl ? (
                                            <img
                                                src={avatarPreviewUrl}
                                                alt="Selected Target Face"
                                                style={{ width: '100%', height: '100%', objectFit: 'contain', background: '#000' }}
                                            />
                                        ) : (
                                            <div style={{ textAlign: 'center', color: '#64748b', padding: '24px' }}>
                                                <span style={{ fontSize: '42px', display: 'block', marginBottom: '10px' }}>👤</span>
                                                <span style={{ fontSize: '14px', color: '#cbd5e1', fontWeight: 600 }}>
                                                    Click <strong>"🖼️ Choose Target Picture / Avatar"</strong> above to load the face you want to swap into!
                                                </span>
                                            </div>
                                        )}

                                        {engineRunning && (
                                            <div
                                                style={{
                                                    position: 'absolute',
                                                    top: '10px',
                                                    right: '10px',
                                                    background: 'rgba(22, 163, 74, 0.92)',
                                                    color: '#fff',
                                                    padding: '4px 10px',
                                                    borderRadius: '6px',
                                                    fontSize: '11px',
                                                    fontWeight: 800
                                                }}
                                            >
                                                LIVE • {liveFps.toFixed(1)} FPS • OUT #{previewTelemetry.cppOutputFrames}
                                            </div>
                                        )}
                                    </div>
                                </div>
                            )}
                        </div>

                        {/* COMPACT STUDIO CONTROL BAR BELOW MONITORS */}
                        <div style={{ display: 'grid', gridTemplateColumns: '1.15fr 0.85fr', gap: '14px' }}>
                            {/* LEFT: VISION & BACKGROUND CONTROLS */}
                            <div style={{ background: '#0f172a', padding: '11px 14px', borderRadius: '12px', border: '1px solid #1e293b', display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px', flexWrap: 'wrap' }}>
                                <div style={{ display: 'flex', gap: '6px', flexWrap: 'wrap', alignItems: 'center' }}>
                                    <button
                                        onClick={() => handleVisionToggle('faceSwapEnabled')}
                                        style={{
                                            padding: '6px 10px',
                                            borderRadius: '6px',
                                            border: '1px solid #334155',
                                            background: settings.faceSwapEnabled ? '#0284c7' : '#1e293b',
                                            color: '#fff',
                                            fontSize: '11px',
                                            fontWeight: 700,
                                            cursor: 'pointer'
                                        }}
                                    >
                                        {settings.faceSwapEnabled ? '✅ Face Swap + Skin Blend' : '⬜ Face Swap Off'}
                                    </button>
                                    <button
                                        onClick={() => handleVisionToggle('lipSyncEnabled')}
                                        style={{
                                            padding: '6px 10px',
                                            borderRadius: '6px',
                                            border: '1px solid #334155',
                                            background: settings.lipSyncEnabled ? '#0284c7' : '#1e293b',
                                            color: '#fff',
                                            fontSize: '11px',
                                            fontWeight: 700,
                                            cursor: 'pointer'
                                        }}
                                    >
                                        {settings.lipSyncEnabled ? '✅ Lip-Sync + Eye Blink' : '⬜ Lip-Sync Off'}
                                    </button>
                                    <button
                                        onClick={() => handleVisionToggle('garmentOverlayEnabled')}
                                        style={{
                                            padding: '6px 10px',
                                            borderRadius: '6px',
                                            border: '1px solid #334155',
                                            background: settings.garmentOverlayEnabled ? '#0284c7' : '#1e293b',
                                            color: '#fff',
                                            fontSize: '11px',
                                            fontWeight: 700,
                                            cursor: 'pointer'
                                        }}
                                    >
                                        {settings.garmentOverlayEnabled ? '✅ Cloth Tone ON' : '👕 Cloth Tone'}
                                    </button>

                                    <span style={{ fontSize: '11px', color: '#64748b', marginLeft: '4px' }}>BG:</span>
                                    {(['disabled', 'blur', 'transparent'] as const).map((m) => (
                                        <button
                                            key={m}
                                            onClick={() => handleBackgroundModeChange(m)}
                                            style={{
                                                padding: '5px 8px',
                                                borderRadius: '6px',
                                                border: '1px solid #334155',
                                                background: backgroundMode === m ? '#38bdf8' : '#020617',
                                                color: backgroundMode === m ? '#020617' : '#cbd5e1',
                                                fontSize: '11px',
                                                fontWeight: 600,
                                                cursor: 'pointer'
                                            }}
                                        >
                                            {m === 'disabled' ? 'Original' : m === 'blur' ? 'Blur' : 'Green'}
                                        </button>
                                    ))}
                                    <button
                                        onClick={handleVirtualBgPick}
                                        style={{
                                            padding: '5px 8px',
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

                                <span style={{ fontSize: '11px', color: '#22c55e', fontWeight: 600 }}>
                                    {kinematics.fullBodyVisible ? '🧍 Full-Body' : '👤 Upper-Body'}
                                    {kinematics.leftArmRaised ? ' • ✋ L-Hand' : ''}
                                    {kinematics.rightArmRaised ? ' • ✋ R-Hand' : ''}
                                    {kinematics.handsHoldingBody ? ' • 🤲 Hold Body' : ''}
                                </span>
                            </div>

                            {/* RIGHT: REAL CLONED VOICE SELECTOR & PITCH */}
                            <div style={{ background: '#0f172a', padding: '11px 14px', borderRadius: '12px', border: '1px solid #38bdf8', display: 'flex', alignItems: 'center', gap: '8px', flexWrap: 'wrap' }}>
                                <select
                                    value={activeProfileId}
                                    onChange={(e) => handleVoiceSelectChange(e.target.value)}
                                    style={{
                                        flex: 1,
                                        minWidth: '170px',
                                        padding: '6px 8px',
                                        background: '#020617',
                                        color: '#f8fafc',
                                        border: '1px solid #38bdf8',
                                        borderRadius: '6px',
                                        fontSize: '12px',
                                        fontWeight: 600
                                    }}
                                >
                                    {voices.length === 0 ? (
                                        <option value="">No Cloned Voice Yet (Go to Voice Cloning Tab)</option>
                                    ) : (
                                        voices.map((v) => (
                                            <option key={v.id} value={v.id}>
                                                🎙️ {v.displayName}
                                            </option>
                                        ))
                                    )}
                                </select>

                                <button
                                    onClick={() => setActiveTab('voices')}
                                    style={{
                                        background: '#38bdf8',
                                        color: '#020617',
                                        border: 'none',
                                        borderRadius: '6px',
                                        padding: '6px 10px',
                                        fontSize: '11px',
                                        fontWeight: 800,
                                        cursor: 'pointer'
                                    }}
                                >
                                    + Clone New Voice
                                </button>

                                <button
                                    onClick={handleToggleVoicePreview}
                                    style={{
                                        background: voicePreviewActive ? '#dc2626' : '#16a34a',
                                        color: '#fff',
                                        border: 'none',
                                        borderRadius: '6px',
                                        padding: '6px 10px',
                                        fontSize: '11px',
                                        fontWeight: 700,
                                        cursor: 'pointer'
                                    }}
                                >
                                    {voicePreviewActive ? '🛑 Mute Test' : '🎧 Test Voice'}
                                </button>
                            </div>
                        </div>

                        {/* COLLAPSIBLE LIVE DEBUG TRACE DRAWER */}
                        {showLogsDrawer && (
                            <div style={{ background: '#0f172a', padding: '10px 14px', borderRadius: '10px', border: '1px solid #334155' }}>
                                <div
                                    style={{
                                        background: '#020617',
                                        padding: '8px',
                                        borderRadius: '6px',
                                        height: '95px',
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
                        )}
                    </div>
                )}

                {/* ==================== TAB 2: ADMIN VOICE CLONING STUDIO & LIBRARY ==================== */}
                {activeTab === 'voices' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid #1e293b', paddingBottom: '14px' }}>
                            <div>
                                <h1 style={{ margin: 0, fontSize: '22px', color: '#38bdf8' }}>
                                    🎙️ Admin Voice Cloning Studio & Saved Library
                                </h1>
                                <p style={{ margin: '4px 0 0', fontSize: '13px', color: '#94a3b8' }}>
                                    Upload any Video (.mp4, .mov) or Audio (.wav, .mp3, .flac, .m4a) or .onnx file to clone and save a real 48kHz voice profile into C++ <code>/models</code>.
                                </p>
                            </div>
                            <button
                                onClick={handleToggleVoicePreview}
                                style={{
                                    background: voicePreviewActive ? '#dc2626' : '#16a34a',
                                    color: '#fff',
                                    border: 'none',
                                    padding: '10px 18px',
                                    borderRadius: '8px',
                                    fontWeight: 700,
                                    cursor: 'pointer'
                                }}
                            >
                                {voicePreviewActive ? '🛑 Stop Live Mic Test' : '🎧 Test Active Cloned Voice in Mic'}
                            </button>
                        </header>

                        {/* STEP-BY-STEP ADMIN VOICE CLONING CARD */}
                        <div
                            style={{
                                background: '#0f172a',
                                padding: '20px',
                                borderRadius: '14px',
                                border: '1px solid #38bdf8',
                                display: 'flex',
                                flexDirection: 'column',
                                gap: '14px'
                            }}
                        >
                            <h3 style={{ margin: 0, fontSize: '16px', color: '#f8fafc' }}>
                                ⚡ Clone & Save a New Voice from Video or Audio
                            </h3>
                            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1.3fr auto', gap: '12px', alignItems: 'center' }}>
                                <input
                                    type="text"
                                    placeholder="Enter Voice Name (e.g., Client CEO Voice)"
                                    value={cloneNameInput}
                                    onChange={(e) => setCloneNameInput(e.target.value)}
                                    style={{
                                        padding: '11px 14px',
                                        background: '#020617',
                                        border: '1px solid #334155',
                                        borderRadius: '8px',
                                        color: '#f8fafc',
                                        fontSize: '13px'
                                    }}
                                />

                                <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                                    <button
                                        onClick={handlePickMediaForVoiceClone}
                                        style={{
                                            background: '#1e293b',
                                            color: '#38bdf8',
                                            border: '1px solid #38bdf8',
                                            padding: '11px 16px',
                                            borderRadius: '8px',
                                            fontWeight: 700,
                                            fontSize: '13px',
                                            cursor: 'pointer',
                                            whiteSpace: 'nowrap'
                                        }}
                                    >
                                        📂 1. Select Video / Audio File
                                    </button>
                                    <span
                                        style={{
                                            fontSize: '12px',
                                            color: cloneSourceFilePath ? '#22c55e' : '#64748b',
                                            overflow: 'hidden',
                                            textOverflow: 'ellipsis',
                                            whiteSpace: 'nowrap'
                                        }}
                                    >
                                        {cloneSourceFilePath || 'No media file selected yet (.mp4, .mov, .wav, .mp3, .onnx)'}
                                    </span>
                                </div>

                                <button
                                    onClick={handleExecuteVoiceCloneAndSave}
                                    disabled={cloningBusy || !cloneSourceFilePath}
                                    style={{
                                        background: cloneSourceFilePath ? '#16a34a' : '#334155',
                                        color: '#fff',
                                        border: 'none',
                                        padding: '11px 20px',
                                        borderRadius: '8px',
                                        fontWeight: 800,
                                        fontSize: '13px',
                                        cursor: cloneSourceFilePath ? 'pointer' : 'not-allowed',
                                        whiteSpace: 'nowrap'
                                    }}
                                >
                                    {cloningBusy ? '⌛ Cloning Voice...' : '⚡ 2. Clone & Save Voice Profile'}
                                </button>
                            </div>

                            {/* REAL-TIME RVC VOICE TUNING SLIDERS */}
                            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: '16px', paddingTop: '8px', borderTop: '1px solid #1e293b' }}>
                                <div>
                                    <div style={{ fontSize: '12px', color: '#cbd5e1', marginBottom: '4px' }}>
                                        Pitch Shift ({settings.pitchShift > 0 ? `+${settings.pitchShift}` : settings.pitchShift} semitones)
                                    </div>
                                    <input
                                        type="range"
                                        min={-12}
                                        max={12}
                                        step={1}
                                        value={settings.pitchShift}
                                        onChange={(e) =>
                                            handleVoiceParamChange(Number(e.target.value), settings.indexRate, settings.protectRate)
                                        }
                                        style={{ width: '100%', cursor: 'pointer' }}
                                    />
                                </div>
                                <div>
                                    <div style={{ fontSize: '12px', color: '#cbd5e1', marginBottom: '4px' }}>
                                        Cloned Timbre Strength ({Math.round(settings.indexRate * 100)}%)
                                    </div>
                                    <input
                                        type="range"
                                        min={0}
                                        max={1}
                                        step={0.05}
                                        value={settings.indexRate}
                                        onChange={(e) =>
                                            handleVoiceParamChange(settings.pitchShift, Number(e.target.value), settings.protectRate)
                                        }
                                        style={{ width: '100%', cursor: 'pointer' }}
                                    />
                                </div>
                                <div>
                                    <div style={{ fontSize: '12px', color: '#cbd5e1', marginBottom: '4px' }}>
                                        Consonant & Breath Protection ({Math.round(settings.protectRate * 100)}%)
                                    </div>
                                    <input
                                        type="range"
                                        min={0}
                                        max={0.5}
                                        step={0.02}
                                        value={settings.protectRate}
                                        onChange={(e) =>
                                            handleVoiceParamChange(settings.pitchShift, settings.indexRate, Number(e.target.value))
                                        }
                                        style={{ width: '100%', cursor: 'pointer' }}
                                    />
                                </div>
                            </div>
                        </div>

                        {/* SAVED CLONED VOICES LIBRARY */}
                        <div>
                            <h3 style={{ margin: '0 0 12px 0', fontSize: '16px', color: '#e2e8f0' }}>
                                Saved Cloned Voices in Engine ({voices.length})
                            </h3>
                            {voices.length === 0 ? (
                                <div style={{ background: '#0f172a', padding: '32px', borderRadius: '12px', border: '1px dashed #334155', textAlign: 'center', color: '#94a3b8' }}>
                                    No cloned voices saved yet. Use the <strong>Clone & Save a New Voice</strong> box above to upload a video or audio file!
                                </div>
                            ) : (
                                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(300px, 1fr))', gap: '14px' }}>
                                    {voices.map((v) => {
                                        const isSelected = v.id === activeProfileId;
                                        return (
                                            <div
                                                key={v.id}
                                                style={{
                                                    background: '#0f172a',
                                                    padding: '16px',
                                                    borderRadius: '12px',
                                                    border: isSelected ? '2px solid #38bdf8' : '1px solid #1e293b',
                                                    display: 'flex',
                                                    flexDirection: 'column',
                                                    gap: '8px'
                                                }}
                                            >
                                                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                                                    <strong style={{ color: '#f8fafc', fontSize: '15px' }}>🎙️ {v.displayName}</strong>
                                                    {isSelected && (
                                                        <span style={{ background: '#16a34a', color: '#fff', padding: '2px 8px', borderRadius: '4px', fontSize: '10px', fontWeight: 800 }}>
                                                            ACTIVE IN CALL
                                                        </span>
                                                    )}
                                                </div>
                                                <div style={{ fontSize: '11px', color: '#94a3b8', wordBreak: 'break-all' }}>
                                                    <strong>Source Media:</strong> {v.sourcePath || 'Compiled ONNX'}
                                                </div>
                                                <div style={{ fontSize: '11px', color: '#38bdf8' }}>
                                                    <strong>Vocal Signature:</strong> F0 Bias {v.f0Bias?.toFixed(1) || '0.0'} st • F1 {v.formantF1?.toFixed(2) || '1.00'} • F2 {v.formantF2?.toFixed(2) || '1.00'}
                                                </div>
                                                <div style={{ display: 'flex', gap: '8px', marginTop: '6px' }}>
                                                    <button
                                                        onClick={() => handleVoiceSelectChange(v.id)}
                                                        style={{
                                                            flex: 1,
                                                            background: isSelected ? '#0284c7' : '#1e293b',
                                                            color: '#fff',
                                                            border: '1px solid #38bdf8',
                                                            padding: '7px 10px',
                                                            borderRadius: '6px',
                                                            fontSize: '12px',
                                                            fontWeight: 700,
                                                            cursor: 'pointer'
                                                        }}
                                                    >
                                                        {isSelected ? '✓ Selected for Call' : 'Use in Live Call'}
                                                    </button>
                                                    <button
                                                        onClick={() => handleDeleteClonedVoice(v.id, v.displayName)}
                                                        style={{
                                                            background: '#1e293b',
                                                            color: '#ef4444',
                                                            border: '1px solid #334155',
                                                            padding: '7px 12px',
                                                            borderRadius: '6px',
                                                            fontSize: '12px',
                                                            fontWeight: 700,
                                                            cursor: 'pointer'
                                                        }}
                                                    >
                                                        🗑️
                                                    </button>
                                                </div>
                                            </div>
                                        );
                                    })}
                                </div>
                            )}
                        </div>
                    </div>
                )}

                {/* ==================== TAB 3: DEVICE ROUTING (INTERACTIVE CONTROLS) ==================== */}
                {activeTab === 'routing' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header style={{ borderBottom: '1px solid #1e293b', paddingBottom: '14px', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                            <div>
                                <h1 style={{ margin: 0, fontSize: '22px', color: '#38bdf8' }}>🎛️ Virtual Device Routing (Zoom, OBS, Discord, Teams)</h1>
                                <p style={{ margin: '4px 0 0', fontSize: '13px', color: '#94a3b8' }}>
                                    Configure and control the C++ shared-memory Virtual Camera and 48kHz Virtual Microphone bridge.
                                </p>
                            </div>
                            <button
                                onClick={handleToggleEngine}
                                style={{
                                    background: engineRunning ? '#dc2626' : '#16a34a',
                                    color: '#fff',
                                    border: 'none',
                                    padding: '10px 18px',
                                    borderRadius: '8px',
                                    fontWeight: 700,
                                    cursor: 'pointer'
                                }}
                            >
                                {engineRunning ? '🛑 Stop Virtual Routing' : '▶ Start Virtual Routing'}
                            </button>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '14px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', gap: '12px' }}>
                                <h3 style={{ margin: 0, color: '#38bdf8', fontSize: '16px' }}>📹 Virtual Camera Shared Memory Sink</h3>
                                <p style={{ fontSize: '13px', color: '#cbd5e1', margin: 0 }}>
                                    Active Input Source: <strong>{activeEngineSource}</strong>
                                </p>
                                <p style={{ fontSize: '13px', color: '#cbd5e1', margin: 0 }}>
                                    Output Stream: <strong>1280x720 BGR24 @ {liveFps > 0 ? `${liveFps.toFixed(1)} FPS` : '30 FPS Target'}</strong>
                                </p>
                                <p style={{ fontSize: '12px', color: '#22c55e', margin: 0 }}>
                                    ✓ POSIX Shared Memory: <code>/AIStudioVirtualCam</code> | Windows: <code>OBSVirtualCamVideo</code>
                                </p>
                                <div style={{ display: 'flex', gap: '8px', marginTop: '6px' }}>
                                    <button
                                        onClick={() => scanCameras(false)}
                                        style={{
                                            background: '#0284c7',
                                            color: '#fff',
                                            border: 'none',
                                            padding: '8px 14px',
                                            borderRadius: '6px',
                                            fontSize: '12px',
                                            fontWeight: 700,
                                            cursor: 'pointer'
                                        }}
                                    >
                                        🔄 Rescan Camera Sources
                                    </button>
                                </div>
                            </div>

                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '14px', border: '1px solid #38bdf8', display: 'flex', flexDirection: 'column', gap: '12px' }}>
                                <h3 style={{ margin: 0, color: '#38bdf8', fontSize: '16px' }}>🎙️ Virtual Microphone 48kHz Bridge</h3>
                                <p style={{ fontSize: '13px', color: '#cbd5e1', margin: 0 }}>
                                    Active Cloned Voice: <strong>{activeVoiceDisplayName}</strong>
                                </p>
                                <p style={{ fontSize: '13px', color: '#cbd5e1', margin: 0 }}>
                                    Routed 10ms Audio Frames: <strong>{kinematics.routedFrames}</strong> | Peak Level: <strong>{Math.round(kinematics.audioPeak * 100)}%</strong>
                                </p>

                                {/* LIVE AUDIO ENERGY BAR */}
                                <div style={{ width: '100%', height: '10px', background: '#020617', borderRadius: '5px', overflow: 'hidden', border: '1px solid #334155' }}>
                                    <div
                                        style={{
                                            width: `${Math.min(100, Math.round(kinematics.speechEnergy * 100))}%`,
                                            height: '100%',
                                            background: '#22c55e',
                                            transition: 'width 0.1s linear'
                                        }}
                                    />
                                </div>

                                <div>
                                    <div style={{ fontSize: '12px', color: '#cbd5e1', marginBottom: '4px' }}>
                                        Microphone Input Gain ({(settings.inputGain || 1.0).toFixed(1)}x)
                                    </div>
                                    <input
                                        type="range"
                                        min={0.2}
                                        max={3.0}
                                        step={0.1}
                                        value={settings.inputGain || 1.0}
                                        onChange={(e) =>
                                            handleVoiceParamChange(
                                                settings.pitchShift,
                                                settings.indexRate,
                                                settings.protectRate,
                                                Number(e.target.value)
                                            )
                                        }
                                        style={{ width: '100%', cursor: 'pointer' }}
                                    />
                                </div>
                            </div>
                        </div>
                    </div>
                )}

                {/* ==================== TAB 4: CLOUD & ONNX MODEL MANAGER ==================== */}
                {activeTab === 'firebase' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid #1e293b', paddingBottom: '14px' }}>
                            <div>
                                <h1 style={{ margin: 0, fontSize: '22px', color: '#38bdf8' }}>☁️ Cloud & Local ONNX Model Manager</h1>
                                <p style={{ margin: '4px 0 0', fontSize: '13px', color: '#94a3b8' }}>
                                    Import custom <code>.onnx</code> neural weights or verify cached models in <code>/models</code>.
                                </p>
                            </div>
                            <div style={{ display: 'flex', gap: '10px' }}>
                                <button
                                    onClick={handleImportCustomOnnxModel}
                                    style={{
                                        background: '#16a34a',
                                        color: '#fff',
                                        border: 'none',
                                        padding: '10px 16px',
                                        borderRadius: '8px',
                                        fontWeight: 700,
                                        cursor: 'pointer'
                                    }}
                                >
                                    + Import .ONNX Model File
                                </button>
                                <button
                                    onClick={async () => {
                                        const res = await EngineIPCClient.syncCloudModels();
                                        if (res.models) setCachedModels(res.models);
                                        addLog('☁️ [ui/src/App.tsx] Synchronized local ONNX model cache with C++ ModelManager.');
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
                            </div>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(280px, 1fr))', gap: '14px' }}>
                            {cachedModels.map((m) => {
                                const isFullWeight = m.size_bytes > 1024;
                                return (
                                    <div key={m.id} style={{ background: '#0f172a', padding: '16px', borderRadius: '12px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', gap: '6px' }}>
                                        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                                            <strong style={{ color: '#38bdf8', fontSize: '14px' }}>{m.id}.onnx</strong>
                                            <span
                                                style={{
                                                    fontSize: '10px',
                                                    padding: '2px 7px',
                                                    borderRadius: '4px',
                                                    background: isFullWeight ? '#16a34a' : '#0284c7',
                                                    color: '#fff',
                                                    fontWeight: 700
                                                }}
                                            >
                                                {isFullWeight ? 'NEURAL WEIGHTS' : 'DSP PROFILE'}
                                            </span>
                                        </div>
                                        <p style={{ fontSize: '11px', color: '#94a3b8', margin: 0, wordBreak: 'break-all' }}>{m.path}</p>
                                        <span style={{ fontSize: '11px', color: '#22c55e' }}>
                                            ✓ Verified ({(m.size_bytes / 1024).toFixed(2)} KB)
                                        </span>
                                    </div>
                                );
                            })}
                        </div>
                    </div>
                )}

                {/* ==================== TAB 5: HARDWARE ACCELERATION & TELEMETRY ==================== */}
                {activeTab === 'settings' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
                        <header style={{ borderBottom: '1px solid #1e293b', paddingBottom: '14px', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                            <div>
                                <h1 style={{ margin: 0, fontSize: '22px', color: '#38bdf8' }}>⚙️ Hardware Acceleration & Full-Body Telemetry</h1>
                                <p style={{ margin: '4px 0 0', fontSize: '13px', color: '#94a3b8' }}>
                                    Live C++20 <code>HardwareManager</code> capabilities and 33-joint <code>BodyTrackerEngine</code> diagnostics.
                                </p>
                            </div>
                            <button
                                onClick={async () => {
                                    const res = await EngineIPCClient.getHardwareProfile();
                                    if (res.status === 'ok') {
                                        setHardwareProfile({
                                            osName: res.os_name || hardwareProfile.osName,
                                            cpuArch: res.cpu_arch || hardwareProfile.cpuArch,
                                            logicalCores: res.logical_cores || hardwareProfile.logicalCores,
                                            totalRamGb: res.total_ram_gb || hardwareProfile.totalRamGb,
                                            provider: res.provider || hardwareProfile.provider
                                        });
                                        addLog('⚙️ [ui/src/App.tsx] Refreshed C++ HardwareManager telemetry.');
                                    }
                                }}
                                style={{
                                    background: '#38bdf8',
                                    color: '#020617',
                                    border: 'none',
                                    padding: '10px 16px',
                                    borderRadius: '8px',
                                    fontWeight: 700,
                                    cursor: 'pointer'
                                }}
                            >
                                🔄 Refresh Hardware Telemetry
                            </button>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '18px' }}>
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '14px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', gap: '10px' }}>
                                <h3 style={{ margin: 0, color: '#38bdf8', fontSize: '16px' }}>💻 C++ HardwareManager System Profile</h3>
                                <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                    • Operating System: <strong>{hardwareProfile.osName}</strong>
                                </p>
                                <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                    • CPU Architecture: <strong>{hardwareProfile.cpuArch} ({hardwareProfile.logicalCores} Logical Cores)</strong>
                                </p>
                                <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                    • Physical Memory: <strong>{hardwareProfile.totalRamGb.toFixed(2)} GB RAM</strong>
                                </p>
                                <p style={{ margin: 0, fontSize: '13px', color: '#22c55e' }}>
                                    • Active AI Execution Provider: <strong>{hardwareProfile.provider}</strong>
                                </p>
                            </div>

                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '14px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', gap: '10px' }}>
                                <h3 style={{ margin: 0, color: '#38bdf8', fontSize: '16px' }}>🧍 33-Joint BodyTrackerEngine Telemetry</h3>
                                <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                    • Left Hand Raised: <strong>{kinematics.leftArmRaised ? 'YES ✋' : 'No'}</strong> | Right Hand Raised: <strong>{kinematics.rightArmRaised ? 'YES ✋' : 'No'}</strong>
                                </p>
                                <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                    • Hands Holding Body / Torso: <strong>{kinematics.handsHoldingBody ? 'DETECTED (Protected) 🤲' : 'Clear'}</strong>
                                </p>
                                <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                    • Head Roll / Tilt Angle: <strong>{kinematics.headTilt.toFixed(1)}°</strong> | Eye Blink State: <strong>{kinematics.eyesBlinking ? 'Blinking' : 'Open'}</strong>
                                </p>
                                <p style={{ margin: 0, fontSize: '13px', color: '#cbd5e1' }}>
                                    • Subject Framing: <strong>{kinematics.fullBodyVisible ? 'Full-Body / Torso Visible' : 'Close-Up Face'}</strong>
                                </p>
                            </div>
                        </div>
                    </div>
                )}
            </main>
        </div>
    );
}

export default App;