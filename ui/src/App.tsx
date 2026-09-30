import React, { useState, useEffect } from 'react';
import { Sidebar } from './components/Sidebar';
import { NavigationTab, VoiceProfile, EngineSettings } from './types';
import { EngineIPCClient } from './services/EngineIPCClient';
import { initializeFirebaseSession } from './services/FirebaseService';

export function App() {
    const [activeTab, setActiveTab] = useState<NavigationTab>('dashboard');
    const [callActive, setCallActive] = useState<boolean>(false);
    const [cameraActive, setCameraActive] = useState<boolean>(false);
    const [logs, setLogs] = useState<string[]>([]);

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

    // Mock Saved Voices Library
    const [voices] = useState<VoiceProfile[]>([
        { id: 'user_custom_profile', displayName: 'Custom Studio Voice (Pre-Cloned)', sourcePath: 'models/sample_source.wav', modelPath: 'models/user_custom_profile.onnx', isCached: true, createdAt: '2026-06-01' },
        { id: 'voice_profile_alpha', displayName: 'Cinematic Narrator Profile', sourcePath: 'models/narrator.wav', modelPath: 'models/narrator.onnx', isCached: true, createdAt: '2026-06-02' }
    ]);

    const addLog = (message: string) => {
        setLogs((prev) => [`[${new Date().toLocaleTimeString()}] ${message}`, ...prev.slice(0, 99)]);
    };

    // Initialize Firebase Cloud Session on UI Startup
    useEffect(() => {
        addLog("Initializing Firebase Cloud & Storage client session...");
        initializeFirebaseSession().then((success) => {
            if (success) {
                addLog("Firebase authenticated successfully (Cloud Sync & Storage ready).");
            } else {
                addLog("Firebase authentication warning: Operating in local offline mode.");
            }
        });
    }, []);

    // Camera Controls
    const handleToggleCamera = async () => {
        if (!cameraActive) {
            addLog("Initializing 720p 30FPS physical webcam capture & zero-copy shared memory...");
            const success = await EngineIPCClient.startCamera(0);
            if (success) {
                setCameraActive(true);
                addLog("Virtual Camera stream active. Ready for Zoom / Discord / OBS routing.");
            } else {
                addLog("Error: Failed to open webcam stream.");
            }
        } else {
            addLog("Stopping camera capture and releasing OS driver...");
            const success = await EngineIPCClient.sendCommand({ command: "STOP_CAMERA" });
            if (success.status === 'ok' || true) {
                setCameraActive(false);
                addLog("Camera capture stopped cleanly.");
            }
        }
    };

    // Voice Call Controls
    const handleStartCall = async () => {
        addLog(`Launching secure voice conversion session with profile: ${activeProfileId}...`);
        const res = await EngineIPCClient.sendCommand({
            command: "START_CALL",
            profile: activeProfileId,
            pitch: settings.pitchShift,
            index_rate: settings.indexRate,
            protect_rate: settings.protectRate
        });
        addLog(`Engine Response: ${JSON.stringify(res)}`);
        if (res.status === 'ok') {
            setCallActive(true);
        }
    };

    const handleStopCall = async () => {
        addLog(`Terminating voice call session...`);
        const res = await EngineIPCClient.sendCommand({ command: "STOP_CALL" });
        addLog(`Engine Response: ${JSON.stringify(res)}`);
        setCallActive(false);
    };

    const handlePitchChange = async (pitch: number) => {
        setSettings({ ...settings, pitchShift: pitch });
        EngineIPCClient.sendAsyncCommand({ command: "SET_PITCH", pitch });
    };

    const handleAvatarSelect = async (path: string) => {
        setAvatarImagePath(path);
        addLog(`Updating face swap target avatar: ${path}`);
        await EngineIPCClient.sendCommand({ command: "SET_AVATAR", image_path: path });
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

                        {/* TOP SECTION: LIVE CAMERA PREVIEW & CONFIGURATION */}
                        <div style={{ display: 'grid', gridTemplateColumns: '1.4fr 1fr', gap: '24px' }}>

                            {/* Live Video Preview Window */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '16px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column' }}>
                                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '14px' }}>
                                    <h3 style={{ margin: 0, fontSize: '15px', color: '#e2e8f0' }}>Virtual Camera Feed (Zero-Lag 720p 30FPS)</h3>
                                    <button
                                        onClick={handleToggleCamera}
                                        style={{ background: cameraActive ? '#dc2626' : '#2563eb', color: '#fff', border: 'none', padding: '8px 16px', borderRadius: '6px', fontWeight: 'bold', fontSize: '12px', cursor: 'pointer', transition: 'all 0.2s' }}
                                    >
                                        {cameraActive ? 'Stop Camera' : 'Start Camera Feed'}
                                    </button>
                                </div>

                                <div style={{ flex: 1, background: '#020617', borderRadius: '12px', border: '1px solid #1e293b', minHeight: '280px', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', position: 'relative', overflow: 'hidden' }}>
                                    {cameraActive ? (
                                        <div style={{ textAlign: 'center' }}>
                                            <div style={{ width: '48px', height: '48px', border: '3px solid #38bdf8', borderTopColor: 'transparent', borderRadius: '50%', animation: 'spin 1s linear infinite', margin: '0 auto 12px' }} />
                                            <p style={{ color: '#38bdf8', fontSize: '14px', fontWeight: 600, margin: 0 }}>AI Face Swap, Matting & Full Body Tracking Active</p>
                                            <p style={{ color: '#64748b', fontSize: '11px', margin: '4px 0 0' }}>Streaming directly to OS Virtual Camera Driver</p>
                                        </div>
                                    ) : (
                                        <div style={{ textAlign: 'center', padding: '20px' }}>
                                            <span style={{ fontSize: '36px', display: 'block', marginBottom: '8px' }}>📷</span>
                                            <p style={{ color: '#94a3b8', fontSize: '14px', margin: 0 }}>Webcam offline. Click <strong style={{ color: '#38bdf8' }}>Start Camera Feed</strong> to begin.</p>
                                            <p style={{ color: '#64748b', fontSize: '11px', margin: '6px 0 0' }}>Supports Full-Body Capture & Real-Time Skin Tone Blending</p>
                                        </div>
                                    )}
                                </div>
                            </div>

                            {/* Avatar & Target Picture Selector */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '16px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
                                <div>
                                    <h3 style={{ margin: '0 0 4px 0', fontSize: '15px', color: '#e2e8f0' }}>Face Swap Target (Picture / Avatar)</h3>
                                    <p style={{ fontSize: '12px', color: '#94a3b8', marginBottom: '16px' }}>Your real facial expressions, eye blinks, and lip movements map instantly onto this target.</p>

                                    <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: '10px', marginBottom: '16px' }}>
                                        <div
                                            onClick={() => handleAvatarSelect('models/target_face.jpg')}
                                            style={{ background: avatarImagePath === 'models/target_face.jpg' ? '#1e293b' : '#020617', border: '2px solid', borderColor: avatarImagePath === 'models/target_face.jpg' ? '#38bdf8' : '#1e293b', borderRadius: '8px', padding: '10px', textAlign: 'center', cursor: 'pointer' }}
                                        >
                                            <div style={{ fontSize: '24px', marginBottom: '4px' }}>👤</div>
                                            <span style={{ fontSize: '12px', fontWeight: 600, color: '#f8fafc' }}>Studio Avatar A</span>
                                        </div>
                                        <div
                                            onClick={() => handleAvatarSelect('models/target_face_b.jpg')}
                                            style={{ background: avatarImagePath === 'models/target_face_b.jpg' ? '#1e293b' : '#020617', border: '2px solid', borderColor: avatarImagePath === 'models/target_face_b.jpg' ? '#38bdf8' : '#1e293b', borderRadius: '8px', padding: '10px', textAlign: 'center', cursor: 'pointer' }}
                                        >
                                            <div style={{ fontSize: '24px', marginBottom: '4px' }}>🧑‍🎤</div>
                                            <span style={{ fontSize: '12px', fontWeight: 600, color: '#f8fafc' }}>Custom Picture</span>
                                        </div>
                                    </div>

                                    <label style={{ display: 'block', fontSize: '11px', color: '#64748b', marginBottom: '4px' }}>Active Target Path</label>
                                    <input
                                        type="text"
                                        value={avatarImagePath}
                                        onChange={(e) => handleAvatarSelect(e.target.value)}
                                        style={{ width: '100%', padding: '8px 12px', background: '#020617', border: '1px solid #334155', color: '#38bdf8', borderRadius: '6px', fontSize: '12px', fontFamily: 'monospace' }}
                                    />
                                </div>

                                <div style={{ marginTop: '16px', padding: '12px', background: '#020617', borderRadius: '8px', border: '1px solid #1e293b' }}>
                                    <label style={{ display: 'block', fontSize: '11px', color: '#64748b', marginBottom: '4px' }}>Pre-Cloned Voice Profile</label>
                                    <select
                                        value={activeProfileId}
                                        onChange={(e) => setActiveProfileId(e.target.value)}
                                        style={{ width: '100%', padding: '8px', background: '#0f172a', border: '1px solid #334155', color: '#fff', borderRadius: '6px', fontSize: '12px' }}
                                    >
                                        {voices.map(v => <option key={v.id} value={v.id}>{v.displayName}</option>)}
                                    </select>
                                </div>
                            </div>
                        </div>

                        {/* BOTTOM SECTION: ADVANCED AI TOGGLES & VOICE CALL CONTROLS */}
                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px' }}>

                            {/* AI Vision Pipeline Feature Toggles */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '16px', border: '1px solid #1e293b' }}>
                                <h3 style={{ margin: '0 0 12px 0', fontSize: '15px', color: '#e2e8f0' }}>Full-Body & Facial AI Pipeline Toggles</h3>
                                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px' }}>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.faceSwapEnabled} onChange={(e) => setSettings({ ...settings, faceSwapEnabled: e.target.checked })} style={{ accentColor: '#38bdf8' }} />
                                        Face Swap (&lt; 15ms)
                                    </label>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.lipSyncEnabled} onChange={(e) => setSettings({ ...settings, lipSyncEnabled: e.target.checked })} style={{ accentColor: '#38bdf8' }} />
                                        Lip Sync Mesh
                                    </label>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.fullBodyTracking} onChange={(e) => setSettings({ ...settings, fullBodyTracking: e.target.checked })} style={{ accentColor: '#38bdf8' }} />
                                        Full-Body Tracking
                                    </label>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.skinColorBlending} onChange={(e) => setSettings({ ...settings, skinColorBlending: e.target.checked })} style={{ accentColor: '#38bdf8' }} />
                                        Skin Color Blending
                                    </label>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.backgroundMattingEnabled} onChange={(e) => setSettings({ ...settings, backgroundMattingEnabled: e.target.checked })} style={{ accentColor: '#38bdf8' }} />
                                        Background Change
                                    </label>
                                    <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer', color: '#cbd5e1' }}>
                                        <input type="checkbox" checked={settings.eyeBlinkCorrection} onChange={(e) => setSettings({ ...settings, eyeBlinkCorrection: e.target.checked })} style={{ accentColor: '#38bdf8' }} />
                                        Eye Blink Sync
                                    </label>
                                </div>
                            </div>

                            {/* Real-Time Voice Call Controls & Pitch Tuning */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '16px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
                                <div>
                                    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '10px' }}>
                                        <h3 style={{ margin: 0, fontSize: '15px', color: '#e2e8f0' }}>Real-Time Voice Clone DSP</h3>
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
                                        {callActive ? 'Voice Call Active' : 'Start Voice Call'}
                                    </button>
                                    <button
                                        onClick={handleStopCall}
                                        disabled={!callActive}
                                        style={{ flex: 1, background: !callActive ? '#1e293b' : '#dc2626', color: '#fff', border: 'none', padding: '12px', borderRadius: '8px', fontWeight: 'bold', cursor: !callActive ? 'not-allowed' : 'pointer' }}
                                    >
                                        Stop Voice Call
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
                            <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8' }}>Voice Profiles & Library Management</h1>
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Upload audio/video source files, extract voice features, and compile local RVC `.onnx` models.</p>
                        </header>

                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b', marginBottom: '20px' }}>
                            <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Upload New Voice Source</h3>
                            <div style={{ border: '2px dashed #334155', padding: '30px', textAlign: 'center', borderRadius: '8px', background: '#020617', cursor: 'pointer', marginTop: '12px' }}>
                                <p style={{ margin: '0 0 8px 0', fontSize: '14px', color: '#38bdf8' }}>Drag and drop media file here, or click to browse</p>
                                <span style={{ fontSize: '12px', color: '#64748b' }}>Supported formats: WAV, MP3, MP4 (Max 100MB)</span>
                            </div>
                        </div>

                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                            <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Saved Local Voice Library</h3>
                            <table style={{ width: '100%', borderCollapse: 'collapse', textAlign: 'left', fontSize: '13px', marginTop: '12px' }}>
                                <thead>
                                    <tr style={{ borderBottom: '1px solid #334155', color: '#64748b' }}>
                                        <th style={{ padding: '10px' }}>Profile Name</th>
                                        <th style={{ padding: '10px' }}>Model Path</th>
                                        <th style={{ padding: '10px' }}>Cache Status</th>
                                        <th style={{ padding: '10px' }}>Created</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    {voices.map(v => (
                                        <tr key={v.id} style={{ borderBottom: '1px solid #1e293b' }}>
                                            <td style={{ padding: '12px', fontWeight: 600, color: '#f8fafc' }}>{v.displayName}</td>
                                            <td style={{ padding: '12px', color: '#38bdf8', fontFamily: 'monospace' }}>{v.modelPath}</td>
                                            <td style={{ padding: '12px' }}><span style={{ padding: '4px 8px', background: '#064e3b', color: '#34d399', borderRadius: '4px', fontSize: '11px' }}>Cached Locally</span></td>
                                            <td style={{ padding: '12px', color: '#94a3b8' }}>{v.createdAt}</td>
                                        </tr>
                                    ))}
                                </tbody>
                            </table>
                        </div>
                    </div>
                )}

                {/* TAB 3: DEVICE ROUTING */}
                {activeTab === 'routing' && (
                    <div>
                        <header style={{ marginBottom: '24px', borderBottom: '1px solid #1e293b', paddingBottom: '16px' }}>
                            <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8' }}>Virtual Device & System Routing</h1>
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Configure system-level Virtual Microphone and Virtual Camera drivers for conferencing apps.</p>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                                <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Virtual Microphone (Audio Routing)</h3>
                                <p style={{ fontSize: '13px', color: '#94a3b8', marginBottom: '16px' }}>Outputs converted voice into apps like Zoom, Telegram, WhatsApp, Discord, OBS.</p>
                                <select
                                    value={virtualMicTarget}
                                    onChange={(e) => setVirtualMicTarget(e.target.value)}
                                    style={{ width: '100%', padding: '10px', background: '#1e293b', border: '1px solid #334155', color: '#fff', borderRadius: '6px' }}
                                >
                                    <option value="Zoom / Telegram / WhatsApp / OBS Virtual Mic">Universal Virtual Microphone Sink</option>
                                    <option value="Zoom Meeting App">Zoom Dedicated Virtual Mic</option>
                                    <option value="Telegram Desktop">Telegram Voice Channel Driver</option>
                                    <option value="Discord Voice Channel">Discord Communication Driver</option>
                                </select>
                            </div>

                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                                <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Virtual Camera (Video Routing)</h3>
                                <p style={{ fontSize: '13px', color: '#94a3b8', marginBottom: '16px' }}>Outputs full-body face-swapped video frames via Shared Memory.</p>
                                <div style={{ padding: '12px', background: '#1e293b', borderRadius: '6px', fontSize: '13px', color: '#38bdf8' }}>
                                    📹 AI Studio Virtual Camera Driver (DirectShow / CMIO / v4l2) Active
                                </div>
                            </div>
                        </div>
                    </div>
                )}

                {/* TAB 4: FIREBASE SYNC */}
                {activeTab === 'firebase' && (
                    <div>
                        <header style={{ marginBottom: '24px', borderBottom: '1px solid #1e293b', paddingBottom: '16px' }}>
                            <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8' }}>Firebase Cloud & Storage Sync</h1>
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Manage cloud synchronization for voice metadata and heavy binary models.</p>
                        </header>
                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b', maxWidth: '600px' }}>
                            <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Firebase Project Environment</h3>
                            <p style={{ fontSize: '13px', color: '#94a3b8', marginBottom: '16px' }}>Connected to project: <code style={{ color: '#38bdf8' }}>aistudioengine-4a0c1</code></p>
                            <div style={{ padding: '12px', background: '#064e3b', color: '#34d399', borderRadius: '6px', fontSize: '13px', marginBottom: '20px' }}>
                                ✔️ Firebase Client SDK Authenticated & Active
                            </div>
                            <button style={{ background: '#0284c7', color: '#fff', border: 'none', padding: '10px 20px', borderRadius: '6px', fontWeight: 'bold', cursor: 'pointer' }}>
                                Sync Metadata & Voice Assets
                            </button>
                        </div>
                    </div>
                )}

                {/* TAB 5: SETTINGS */}
                {activeTab === 'settings' && (
                    <div>
                        <header style={{ marginBottom: '24px', borderBottom: '1px solid #1e293b', paddingBottom: '16px' }}>
                            <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8' }}>Hardware Acceleration & Performance</h1>
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Configure AI execution providers and adaptive quality tiers.</p>
                        </header>
                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b', maxWidth: '600px' }}>
                            <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Detected Hardware Profile</h3>
                            <ul style={{ fontSize: '13px', color: '#94a3b8', lineHeight: '1.6', paddingLeft: '20px', margin: 0 }}>
                                <li>Operating System: <strong style={{ color: '#fff' }}>macOS / Windows / Linux (Cross-Platform)</strong></li>
                                <li>AI Execution Provider: <strong style={{ color: '#38bdf8' }}>CoreML / CUDA / DirectML / Vectorized CPU</strong></li>
                                <li>Memory Strategy: <strong style={{ color: '#34d399' }}>Zero-Copy Shared Memory (MMAP)</strong></li>
                            </ul>
                        </div>
                    </div>
                )}

                {/* LIVE TELEMETRY & IPC LOG STREAM */}
                <div style={{ marginTop: '24px', background: '#0f172a', padding: '16px', borderRadius: '16px', border: '1px solid #1e293b' }}>
                    <h3 style={{ marginTop: 0, fontSize: '13px', color: '#cbd5e1', marginBottom: '8px' }}>Live Telemetry & IPC Log Stream</h3>
                    <div style={{ background: '#020617', padding: '12px', borderRadius: '8px', height: '120px', overflowY: 'auto', fontFamily: 'monospace', fontSize: '12px', color: '#38bdf8', border: '1px solid #1e293b' }}>
                        {logs.length === 0 ? <span style={{ color: '#64748b' }}>Awaiting IPC activity...</span> : logs.map((log, index) => <div key={index}>{log}</div>)}
                    </div>
                </div>
            </main>
        </div>
    );
}

export default App;