import React, { useState, useEffect, useRef } from 'react';
import { Sidebar } from './components/Sidebar';
import { NavigationTab, VoiceProfile, EngineSettings } from './types';
import { EngineIPCClient } from './services/EngineIPCClient';
import { initializeFirebaseSession } from './services/FirebaseService';

export function App() {
    const [activeTab, setActiveTab] = useState<NavigationTab>('dashboard');
    const [callActive, setCallActive] = useState<boolean>(false);
    const [cameraActive, setCameraActive] = useState<boolean>(false);
    const [logs, setLogs] = useState<string[]>([]);

    // 🚀 NEW: Hardware Detection States
    const [cameras, setCameras] = useState<{ id: string; label: string; index: number }[]>([]);
    const [selectedCameraIndex, setSelectedCameraIndex] = useState<number>(0);

    // State for Routing & Profiles
    const [selectedMic] = useState<string>('Default System Microphone');
    const [virtualMicTarget, setVirtualMicTarget] = useState<string>('Zoom / Telegram / WhatsApp / OBS Virtual Mic');
    const [activeProfileId, setActiveProfileId] = useState<string>('user_custom_profile');
    const [avatarImagePath, setAvatarImagePath] = useState<string>('models/target_face.jpg');

    // State for Settings & AI Features
    const [settings, setSettings] = useState<EngineSettings & {
        fullBodyTracking: boolean;
        skinColorBlending: boolean;
        eyeBlinkCorrection: boolean;
    }>({
        pitchShift: 0,
        indexRate: 0.85,
        protectRate: 0.33,
        faceSwapEnabled: true,
        lipSyncEnabled: true,
        backgroundMattingEnabled: true,
        garmentOverlayEnabled: false,
        performanceMode: 'Performance',
        fullBodyTracking: true,
        skinColorBlending: true,
        eyeBlinkCorrection: true
    });

    const [voices, setVoices] = useState<VoiceProfile[]>([
        { id: 'user_custom_profile', displayName: 'Custom Studio Voice (Pre-Cloned)', sourcePath: 'models/sample_source.wav', modelPath: 'models/user_custom_profile.onnx', isCached: true, createdAt: '2026-06-01' },
        { id: 'voice_profile_alpha', displayName: 'Cinematic Narrator Profile', sourcePath: 'models/narrator.wav', modelPath: 'models/narrator.onnx', isCached: true, createdAt: '2026-06-02' }
    ]);

    const addLog = (message: string) => {
        setLogs((prev) => [`[${new Date().toLocaleTimeString()}] ${message}`, ...prev.slice(0, 99)]);
    };

    // 🚀 INITIALIZATION: Detect Physical Hardware Cameras
    useEffect(() => {
        addLog("Initializing Firebase Cloud & Storage client session...");
        initializeFirebaseSession().then((success) => {
            if (success) addLog("Firebase authenticated successfully (Cloud Sync & Storage ready).");
            else addLog("Firebase authentication warning: Operating in local offline mode.");
        });

        const getCameras = async () => {
            try {
                // Request temporary permission to read hardware names
                await navigator.mediaDevices.getUserMedia({ video: true });
                const devices = await navigator.mediaDevices.enumerateDevices();
                const videoInputs = devices
                    .filter(device => device.kind === 'videoinput')
                    .map((dev, index) => ({
                        id: dev.deviceId,
                        label: dev.label || `USB Web Camera ${index}`,
                        index: index // OpenCV maps hardware index exactly to OS enumeration order
                    }));

                if (videoInputs.length > 0) {
                    setCameras(videoInputs);
                    setSelectedCameraIndex(videoInputs[0].index);
                    addLog(`Detected ${videoInputs.length} physical webcams on system.`);
                }
            } catch (err) {
                addLog("Warning: Could not enumerate physical cameras. Falling back to default index 0.");
                setCameras([{ id: 'default', label: 'Default System Camera', index: 0 }]);
            }
        };
        getCameras();
    }, []);

    // ========================================================
    // ⚡ ZERO-LAG FILE SYSTEM IPC HANDLERS
    // ========================================================

    // Extracts the absolute OS path from the file picker
    const getLocalFilePath = (file: File): string => {
        // @ts-ignore - file.path is available in Electron/Tauri wrappers
        return file.path || `models/${file.name}`;
    };

    const handleAvatarUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (!file) return;

        const absolutePath = getLocalFilePath(file);
        setAvatarImagePath(absolutePath);
        addLog(`Injecting high-res avatar to C++ memory: ${absolutePath}`);

        const success = await EngineIPCClient.setFaceSwapAvatar(absolutePath);
        if (success) addLog(`Face swap avatar updated live with zero lag.`);
    };

    const handleVoiceUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (!file) return;

        const absolutePath = getLocalFilePath(file);
        const newVoiceId = `custom_voice_${Date.now()}`;

        addLog(`Loading custom voice model into AI DSP: ${absolutePath}`);

        setVoices(prev => [...prev, {
            id: newVoiceId,
            displayName: file.name,
            sourcePath: absolutePath,
            modelPath: absolutePath,
            isCached: true,
            createdAt: new Date().toISOString().split('T')[0]
        }]);

        setActiveProfileId(newVoiceId);
        await EngineIPCClient.setVoiceProfile(absolutePath);
    };

    // ========================================================
    // 🎥 HIGH-SPEED ENGINE CONTROLS
    // ========================================================

    const handleToggleCamera = async () => {
        if (!cameraActive) {
            addLog(`Initializing 720p 30FPS physical webcam (Index: ${selectedCameraIndex})...`);
            const success = await EngineIPCClient.startCamera(selectedCameraIndex);
            if (success) {
                setCameraActive(true);
                addLog("Virtual Camera stream active. Ready for Zoom / Discord / OBS routing.");
            } else {
                addLog("Error: C++ Engine failed to open webcam stream.");
            }
        } else {
            addLog("Stopping camera capture and releasing OS driver...");
            const success = await EngineIPCClient.stopCamera();
            if (success) {
                setCameraActive(false);
                addLog("Camera capture stopped cleanly.");
            }
        }
    };

    const handleStartCall = async () => {
        addLog(`Launching secure voice conversion session with profile: ${activeProfileId}...`);
        const res = await EngineIPCClient.sendCommand({
            command: "START_CALL",
            profile: activeProfileId,
            pitch: settings.pitchShift,
            index_rate: settings.indexRate,
            protect_rate: settings.protectRate
        });
        if (res.status === 'ok') {
            setCallActive(true);
            addLog(`Zero-Lag Voice session live. Target Latency: ${res.latency_target_ms}ms`);
        } else {
            addLog(`Error: ${res.message}`);
        }
    };

    const handleStopCall = async () => {
        addLog(`Terminating voice call session...`);
        const res = await EngineIPCClient.sendCommand({ command: "STOP_CALL" });
        if (res.status === 'ok') setCallActive(false);
    };

    const handlePitchChange = (pitch: number) => {
        setSettings({ ...settings, pitchShift: pitch });
        EngineIPCClient.sendAsyncCommand({ command: "SET_PITCH", pitch });
    };

    return (
        <div style={{ display: 'flex', height: '100vh', background: '#020617', color: '#f8fafc', fontFamily: 'system-ui, -apple-system, sans-serif', overflow: 'hidden' }}>
            <Sidebar activeTab={activeTab} setActiveTab={setActiveTab} callActive={callActive || cameraActive} />

            <main style={{ flex: 1, display: 'flex', flexDirection: 'column', height: '100vh', overflowY: 'auto', padding: '32px' }}>

                {/* TAB 1: LIVE STUDIO CALL DASHBOARD */}
                {activeTab === 'dashboard' && (
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
                        <header style={{ borderBottom: '1px solid #1e293b', paddingBottom: '16px', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                            <div>
                                <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8', fontWeight: 700 }}>Live Studio Video & Voice Suite</h1>
                                <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Real-time full-body tracking, photo face-swap, skin color blending, and pre-cloned voice conversion.</p>
                            </div>
                            <div style={{ display: 'flex', gap: '10px' }}>
                                <span style={{ padding: '6px 12px', background: cameraActive ? '#064e3b' : '#1e293b', color: cameraActive ? '#34d399' : '#94a3b8', borderRadius: '6px', fontSize: '12px', fontWeight: 600 }}>
                                    {cameraActive ? '📹 Camera Streaming' : '📹 Camera Idle'}
                                </span>
                                <span style={{ padding: '6px 12px', background: callActive ? '#064e3b' : '#1e293b', color: callActive ? '#34d399' : '#94a3b8', borderRadius: '6px', fontSize: '12px', fontWeight: 600 }}>
                                    {callActive ? '🎙️ Voice Active' : '🎙️ Voice Idle'}
                                </span>
                            </div>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: '1.4fr 1fr', gap: '24px' }}>

                            {/* Live Video Preview Window */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '16px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column' }}>
                                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '14px', flexWrap: 'wrap', gap: '10px' }}>
                                    <h3 style={{ margin: 0, fontSize: '15px', color: '#e2e8f0' }}>Virtual Camera Feed</h3>

                                    {/* 🚀 DYNAMIC CAMERA SELECTOR */}
                                    <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                                        <select
                                            value={selectedCameraIndex}
                                            onChange={(e) => setSelectedCameraIndex(Number(e.target.value))}
                                            disabled={cameraActive}
                                            style={{ padding: '6px 10px', background: '#020617', border: '1px solid #334155', color: '#38bdf8', borderRadius: '6px', fontSize: '12px', outline: 'none' }}
                                        >
                                            {cameras.map(cam => (
                                                <option key={cam.id} value={cam.index}>{cam.label}</option>
                                            ))}
                                        </select>
                                        <button
                                            onClick={handleToggleCamera}
                                            style={{ background: cameraActive ? '#dc2626' : '#2563eb', color: '#fff', border: 'none', padding: '8px 16px', borderRadius: '6px', fontWeight: 'bold', fontSize: '12px', cursor: 'pointer', transition: 'all 0.2s' }}
                                        >
                                            {cameraActive ? 'Stop Camera' : 'Start Camera Feed'}
                                        </button>
                                    </div>
                                </div>

                                <div style={{ flex: 1, background: '#020617', borderRadius: '12px', border: '1px solid #1e293b', minHeight: '280px', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', position: 'relative', overflow: 'hidden' }}>
                                    {cameraActive ? (
                                        <div style={{ textAlign: 'center' }}>
                                            <div style={{ width: '48px', height: '48px', border: '3px solid #38bdf8', borderTopColor: 'transparent', borderRadius: '50%', animation: 'spin 1s linear infinite', margin: '0 auto 12px' }} />
                                            <p style={{ color: '#38bdf8', fontSize: '14px', fontWeight: 600, margin: 0 }}>AI Face Swap & Tracking Active</p>
                                            <p style={{ color: '#64748b', fontSize: '11px', margin: '4px 0 0' }}>Streaming directly to OS Virtual Camera</p>
                                        </div>
                                    ) : (
                                        <div style={{ textAlign: 'center', padding: '20px' }}>
                                            <span style={{ fontSize: '36px', display: 'block', marginBottom: '8px' }}>📷</span>
                                            <p style={{ color: '#94a3b8', fontSize: '14px', margin: 0 }}>Webcam offline. Click <strong style={{ color: '#38bdf8' }}>Start Camera Feed</strong>.</p>
                                        </div>
                                    )}
                                </div>
                            </div>

                            {/* Avatar & Target Picture Selector */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '16px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
                                <div>
                                    <h3 style={{ margin: '0 0 4px 0', fontSize: '15px', color: '#e2e8f0' }}>Face Swap Target (Avatar)</h3>
                                    <p style={{ fontSize: '12px', color: '#94a3b8', marginBottom: '16px' }}>Upload any picture to seamlessly map your face in real-time.</p>

                                    {/* 🚀 NATIVE OS FILE PICKER FOR PICTURES */}
                                    <div style={{ background: '#020617', border: '2px dashed #334155', borderRadius: '8px', padding: '20px', textAlign: 'center', marginBottom: '16px' }}>
                                        <label htmlFor="avatar-upload" style={{ cursor: 'pointer', display: 'block' }}>
                                            <div style={{ fontSize: '28px', marginBottom: '8px' }}>🖼️</div>
                                            <span style={{ fontSize: '14px', fontWeight: 600, color: '#38bdf8' }}>Choose Picture from System</span>
                                            <div style={{ fontSize: '11px', color: '#64748b', marginTop: '4px' }}>JPG, PNG files supported</div>
                                        </label>
                                        <input
                                            id="avatar-upload"
                                            type="file"
                                            accept="image/*"
                                            onChange={handleAvatarUpload}
                                            style={{ display: 'none' }}
                                        />
                                    </div>

                                    <label style={{ display: 'block', fontSize: '11px', color: '#64748b', marginBottom: '4px' }}>Active Target Path</label>
                                    <input
                                        type="text"
                                        value={avatarImagePath}
                                        onChange={(e) => {
                                            setAvatarImagePath(e.target.value);
                                            EngineIPCClient.setFaceSwapAvatar(e.target.value);
                                        }}
                                        style={{ width: '100%', padding: '8px 12px', background: '#020617', border: '1px solid #334155', color: '#38bdf8', borderRadius: '6px', fontSize: '12px', fontFamily: 'monospace' }}
                                    />
                                </div>
                            </div>
                        </div>

                        {/* BOTTOM SECTION: ADVANCED AI TOGGLES & VOICE CALL CONTROLS */}
                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px' }}>

                            {/* AI Vision Pipeline Feature Toggles */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '16px', border: '1px solid #1e293b' }}>
                                <h3 style={{ margin: '0 0 12px 0', fontSize: '15px', color: '#e2e8f0' }}>Vision Pipeline Toggles</h3>
                                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px' }}>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.faceSwapEnabled} onChange={(e) => setSettings({ ...settings, faceSwapEnabled: e.target.checked })} style={{ accentColor: '#38bdf8' }} /> Face Swap
                                    </label>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.lipSyncEnabled} onChange={(e) => setSettings({ ...settings, lipSyncEnabled: e.target.checked })} style={{ accentColor: '#38bdf8' }} /> Lip Sync Mesh
                                    </label>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.backgroundMattingEnabled} onChange={(e) => setSettings({ ...settings, backgroundMattingEnabled: e.target.checked })} style={{ accentColor: '#38bdf8' }} /> Background Blur
                                    </label>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.skinColorBlending} onChange={(e) => setSettings({ ...settings, skinColorBlending: e.target.checked })} style={{ accentColor: '#38bdf8' }} /> Skin Blending
                                    </label>
                                </div>
                            </div>

                            {/* Real-Time Voice Call Controls & Pitch Tuning */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '16px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
                                <div>
                                    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '10px' }}>
                                        <h3 style={{ margin: 0, fontSize: '15px', color: '#e2e8f0' }}>Real-Time Voice DSP</h3>
                                        <span style={{ fontSize: '12px', color: '#38bdf8', fontWeight: 600 }}>Pitch: {settings.pitchShift} St</span>
                                    </div>
                                    <input
                                        type="range" min="-12" max="12" value={settings.pitchShift}
                                        onChange={(e) => handlePitchChange(parseInt(e.target.value))}
                                        style={{ width: '100%', accentColor: '#38bdf8', marginBottom: '16px' }}
                                    />
                                </div>

                                <div style={{ display: 'flex', gap: '12px' }}>
                                    <button
                                        onClick={handleStartCall}
                                        disabled={callActive}
                                        style={{ flex: 1, background: callActive ? '#1e293b' : '#0284c7', color: '#fff', border: 'none', padding: '12px', borderRadius: '8px', fontWeight: 'bold', cursor: callActive ? 'not-allowed' : 'pointer' }}
                                    >
                                        {callActive ? 'Voice Routing Active' : 'Start Voice Call'}
                                    </button>
                                    <button
                                        onClick={handleStopCall}
                                        disabled={!callActive}
                                        style={{ flex: 1, background: !callActive ? '#1e293b' : '#dc2626', color: '#fff', border: 'none', padding: '12px', borderRadius: '8px', fontWeight: 'bold', cursor: !callActive ? 'not-allowed' : 'pointer' }}
                                    >
                                        Stop Routing
                                    </button>
                                </div>
                            </div>
                        </div>
                    </div>
                )}

                {/* TAB 2: VOICE PROFILES & LIBRARY */}
                {activeTab === 'voices' && (
                    <div>
                        <header style={{ marginBottom: '24px', borderBottom: '1px solid #1e293b', paddingBottom: '16px' }}>
                            <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8' }}>Voice Profiles Library</h1>
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Upload your pre-cloned voice models from your system.</p>
                        </header>

                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b', marginBottom: '20px' }}>
                            <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Upload Local Voice Model</h3>

                            {/* 🚀 NATIVE OS FILE PICKER FOR AUDIO/VOICE MODELS */}
                            <label htmlFor="voice-upload" style={{ display: 'block', border: '2px dashed #334155', padding: '30px', textAlign: 'center', borderRadius: '8px', background: '#020617', cursor: 'pointer', marginTop: '12px', transition: 'all 0.2s' }}>
                                <div style={{ fontSize: '32px', marginBottom: '8px' }}>🎙️</div>
                                <p style={{ margin: '0 0 8px 0', fontSize: '15px', color: '#38bdf8', fontWeight: 'bold' }}>Click to select a cloned voice file from your system</p>
                                <span style={{ fontSize: '12px', color: '#64748b' }}>Select .wav, .mp3, .pth, or .onnx files</span>
                            </label>
                            <input
                                id="voice-upload"
                                type="file"
                                accept=".wav,.mp3,.pth,.onnx,audio/*"
                                onChange={handleVoiceUpload}
                                style={{ display: 'none' }}
                            />
                        </div>

                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                            <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Active System Voices</h3>
                            <table style={{ width: '100%', borderCollapse: 'collapse', textAlign: 'left', fontSize: '13px', marginTop: '12px' }}>
                                <thead>
                                    <tr style={{ borderBottom: '1px solid #334155', color: '#64748b' }}>
                                        <th style={{ padding: '10px' }}>Voice Name</th>
                                        <th style={{ padding: '10px' }}>Local File Path</th>
                                        <th style={{ padding: '10px' }}>Selection</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    {voices.map(v => (
                                        <tr key={v.id} style={{ borderBottom: '1px solid #1e293b', background: activeProfileId === v.id ? '#020617' : 'transparent' }}>
                                            <td style={{ padding: '12px', fontWeight: 600, color: '#f8fafc' }}>{v.displayName}</td>
                                            <td style={{ padding: '12px', color: '#64748b', fontFamily: 'monospace' }}>{v.sourcePath}</td>
                                            <td style={{ padding: '12px' }}>
                                                <button
                                                    onClick={() => {
                                                        setActiveProfileId(v.id);
                                                        EngineIPCClient.setVoiceProfile(v.sourcePath);
                                                        addLog(`Active voice swapped to: ${v.displayName}`);
                                                    }}
                                                    style={{ padding: '6px 12px', background: activeProfileId === v.id ? '#38bdf8' : '#1e293b', color: activeProfileId === v.id ? '#020617' : '#f8fafc', border: 'none', borderRadius: '4px', cursor: 'pointer', fontWeight: 'bold' }}
                                                >
                                                    {activeProfileId === v.id ? 'Active' : 'Select'}
                                                </button>
                                            </td>
                                        </tr>
                                    ))}
                                </tbody>
                            </table>
                        </div>
                    </div>
                )}

                {/* TAB 3: DEVICE ROUTING (Now 100% accurate information) */}
                {activeTab === 'routing' && (
                    <div>
                        <header style={{ marginBottom: '24px', borderBottom: '1px solid #1e293b', paddingBottom: '16px' }}>
                            <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8' }}>Virtual Device Routing</h1>
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>How to connect AI Studio Engine to your favorite apps.</p>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                                <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>1. Connecting Video (Zoom/OBS/Discord)</h3>
                                <p style={{ fontSize: '13px', color: '#94a3b8', marginBottom: '16px', lineHeight: '1.6' }}>
                                    When you click <strong>"Start Camera Feed"</strong> on the Dashboard, the C++ engine intercepts your physical webcam and spawns a new OS-level Virtual Camera.
                                </p>
                                <div style={{ padding: '12px', background: '#1e293b', borderRadius: '6px', fontSize: '13px', color: '#38bdf8' }}>
                                    ✅ Open Zoom/Discord settings and simply select <br /><strong>"OBS Virtual Camera"</strong> or <strong>"AIStudio Virtual Cam"</strong> from the camera dropdown list!
                                </div>
                            </div>

                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                                <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>2. Connecting Audio</h3>
                                <p style={{ fontSize: '13px', color: '#94a3b8', marginBottom: '16px', lineHeight: '1.6' }}>
                                    When you click <strong>"Start Voice Call"</strong>, the engine creates a virtual microphone channel in your OS.
                                </p>
                                <div style={{ padding: '12px', background: '#1e293b', borderRadius: '6px', fontSize: '13px', color: '#34d399' }}>
                                    ✅ In your conferencing app, set your Microphone to <br /><strong>"AI Studio Virtual Audio Cable"</strong>.
                                </div>
                            </div>
                        </div>
                    </div>
                )}

                {/* LIVE TELEMETRY & IPC LOG STREAM */}
                <div style={{ marginTop: '24px', background: '#0f172a', padding: '16px', borderRadius: '16px', border: '1px solid #1e293b' }}>
                    <h3 style={{ marginTop: 0, fontSize: '13px', color: '#cbd5e1', marginBottom: '8px' }}>Live IPC Telemetry Stream</h3>
                    <div style={{ background: '#020617', padding: '12px', borderRadius: '8px', height: '120px', overflowY: 'auto', fontFamily: 'monospace', fontSize: '12px', color: '#38bdf8', border: '1px solid #1e293b' }}>
                        {logs.length === 0 ? <span style={{ color: '#64748b' }}>Awaiting IPC activity...</span> : logs.map((log, index) => <div key={index}>{log}</div>)}
                    </div>
                </div>
            </main>
        </div>
    );
}

export default App;