import React, { useState, useEffect } from 'react';
import { Sidebar } from './components/Sidebar';
import { NavigationTab, VoiceProfile, EngineSettings } from './types';
import { EngineIPCClient } from './services/EngineIPCClient';
import { initializeFirebaseSession } from './services/FirebaseService';

export function App() {
    const [activeTab, setActiveTab] = useState<NavigationTab>('dashboard');
    const [callActive, setCallActive] = useState<boolean>(false);
    const [logs, setLogs] = useState<string[]>([]);

    // State for Routing & Profiles
    const [selectedMic] = useState<string>('Default System Microphone');
    const [virtualMicTarget, setVirtualMicTarget] = useState<string>('Zoom / Telegram / WhatsApp / OBS Virtual Mic');
    const [activeProfileId, setActiveProfileId] = useState<string>('user_custom_profile');

    // State for Settings & AI Features
    const [settings, setSettings] = useState<EngineSettings>({
        pitchShift: 2,
        indexRate: 0.85,
        protectRate: 0.33,
        faceSwapEnabled: true,
        lipSyncEnabled: true,
        backgroundMattingEnabled: false,
        garmentOverlayEnabled: false,
        performanceMode: 'Balanced'
    });

    // Mock Saved Voices Library
    const [voices] = useState<VoiceProfile[]>([
        { id: 'user_custom_profile', displayName: 'Custom Studio Voice', sourcePath: 'models/sample_source.wav', modelPath: 'models/user_custom_profile.onnx', isCached: true, createdAt: '2026-06-01' },
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

    const handleStartCall = async () => {
        addLog(`Launching secure call session with profile: ${activeProfileId} -> Target: ${virtualMicTarget}`);
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
        addLog(`Terminating call session...`);
        const res = await EngineIPCClient.sendCommand({ command: "STOP_CALL" });
        addLog(`Engine Response: ${JSON.stringify(res)}`);
        setCallActive(false);
    };

    const handlePitchChange = async (pitch: number) => {
        setSettings({ ...settings, pitchShift: pitch });
        await EngineIPCClient.sendCommand({ command: "SET_PITCH", pitch });
    };

    return (
        <div style={{ display: 'flex', height: '100vh', background: '#020617', color: '#f8fafc', fontFamily: 'system-ui, sans-serif', overflow: 'hidden' }}>
            <Sidebar activeTab={activeTab} setActiveTab={setActiveTab} callActive={callActive} />

            <main style={{ flex: 1, display: 'flex', flexDirection: 'column', height: '100vh', overflowY: 'auto', padding: '32px' }}>
                {/* TAB 1: LIVE STUDIO CALL DASHBOARD */}
                {activeTab === 'dashboard' && (
                    <div>
                        <header style={{ marginBottom: '24px', borderBottom: '1px solid #1e293b', paddingBottom: '16px' }}>
                            <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8' }}>Live Studio Call Dashboard</h1>
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Manage real-time voice conversion, facial identity replacement, and virtual driver routing.</p>
                        </header>

                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px', marginBottom: '24px' }}>
                            {/* Active Voice & Device Routing Summary */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                                <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0', marginBottom: '16px' }}>Active Session Configuration</h3>

                                <div style={{ marginBottom: '14px' }}>
                                    <label style={{ display: 'block', fontSize: '12px', color: '#64748b', marginBottom: '4px' }}>Selected Voice Profile</label>
                                    <select
                                        value={activeProfileId}
                                        onChange={(e) => setActiveProfileId(e.target.value)}
                                        style={{ width: '100%', padding: '10px', background: '#1e293b', border: '1px solid #334155', color: '#fff', borderRadius: '6px' }}
                                    >
                                        {voices.map(v => <option key={v.id} value={v.id}>{v.displayName} ({v.modelPath})</option>)}
                                    </select>
                                </div>

                                <div style={{ marginBottom: '14px' }}>
                                    <label style={{ display: 'block', fontSize: '12px', color: '#64748b', marginBottom: '4px' }}>Input Microphone Source</label>
                                    <div style={{ padding: '10px', background: '#1e293b', border: '1px solid #334155', borderRadius: '6px', fontSize: '13px', color: '#cbd5e1' }}>
                                        🎤 {selectedMic}
                                    </div>
                                </div>

                                <div>
                                    <label style={{ display: 'block', fontSize: '12px', color: '#64748b', marginBottom: '4px' }}>Virtual Device Output Target</label>
                                    <div style={{ padding: '10px', background: '#1e293b', border: '1px solid #334155', borderRadius: '6px', fontSize: '13px', color: '#38bdf8' }}>
                                        🎛️ {virtualMicTarget}
                                    </div>
                                </div>
                            </div>

                            {/* Call Control & Parameter Tuning */}
                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b', display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
                                <div>
                                    <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0', marginBottom: '16px' }}>Real-Time DSP Tuning</h3>

                                    <div style={{ marginBottom: '16px' }}>
                                        <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '12px', color: '#94a3b8', marginBottom: '6px' }}>
                                            <span>Pitch Shift (Semitones)</span>
                                            <span style={{ color: '#38bdf8', fontWeight: 600 }}>{settings.pitchShift}</span>
                                        </div>
                                        <input
                                            type="range" min="-12" max="12" value={settings.pitchShift}
                                            onChange={(e) => handlePitchChange(parseInt(e.target.value))}
                                            style={{ width: '100%', accentColor: '#38bdf8' }}
                                        />
                                    </div>

                                    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '10px' }}>
                                        <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer' }}>
                                            <input type="checkbox" checked={settings.faceSwapEnabled} onChange={(e) => setSettings({ ...settings, faceSwapEnabled: e.target.checked })} />
                                            Face Swap (&lt;15ms)
                                        </label>
                                        <label style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '13px', cursor: 'pointer' }}>
                                            <input type="checkbox" checked={settings.lipSyncEnabled} onChange={(e) => setSettings({ ...settings, lipSyncEnabled: e.target.checked })} />
                                            Lip Sync Mesh
                                        </label>
                                    </div>
                                </div>

                                <div style={{ display: 'flex', gap: '12px', marginTop: '20px' }}>
                                    <button
                                        onClick={handleStartCall}
                                        disabled={callActive}
                                        style={{ flex: 1, background: callActive ? '#1e293b' : '#0284c7', color: '#fff', border: 'none', padding: '12px', borderRadius: '8px', fontWeight: 'bold', cursor: callActive ? 'not-allowed' : 'pointer' }}
                                    >
                                        {callActive ? 'Call Active' : 'Start Call'}
                                    </button>
                                    <button
                                        onClick={handleStopCall}
                                        disabled={!callActive}
                                        style={{ flex: 1, background: !callActive ? '#1e293b' : '#dc2626', color: '#fff', border: 'none', padding: '12px', borderRadius: '8px', fontWeight: 'bold', cursor: !callActive ? 'not-allowed' : 'pointer' }}
                                    >
                                        Stop Call
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
                            <p style={{ fontSize: '13px', color: '#94a3b8' }}>Select an audio (.wav/.mp3) or video (.mp4) file containing target speech for preprocessing.</p>
                            <div style={{ border: '2px dashed #334155', padding: '30px', textAlign: 'center', borderRadius: '8px', background: '#020617', cursor: 'pointer' }}>
                                <p style={{ margin: '0 0 8px 0', fontSize: '14px', color: '#38bdf8' }}>Drag and drop media file here, or click to browse</p>
                                <span style={{ fontSize: '12px', color: '#64748b' }}>Supported formats: WAV, MP3, MP4, AAC (Max 100MB)</span>
                            </div>
                        </div>

                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                            <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Saved Local Voice Library</h3>
                            <table style={{ width: '100%', borderCollapse: 'collapse', textAlign: 'left', fontSize: '13px' }}>
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
                                <p style={{ fontSize: '13px', color: '#94a3b8', marginBottom: '16px' }}>Outputs converted voice into apps like Zoom, Telegram, WhatsApp, Discord, OBS, and Google Meet.</p>

                                <label style={{ display: 'block', fontSize: '12px', color: '#64748b', marginBottom: '6px' }}>Virtual Driver Target App</label>
                                <select
                                    value={virtualMicTarget}
                                    onChange={(e) => setVirtualMicTarget(e.target.value)}
                                    style={{ width: '100%', padding: '10px', background: '#1e293b', border: '1px solid #334155', color: '#fff', borderRadius: '6px', marginBottom: '16px' }}
                                >
                                    <option value="Zoom / Telegram / WhatsApp / OBS Virtual Mic">Universal Virtual Microphone Sink</option>
                                    <option value="Zoom Meeting App">Zoom Dedicated Virtual Mic</option>
                                    <option value="Telegram Desktop">Telegram Voice Channel Driver</option>
                                    <option value="OBS Studio Virtual Audio">OBS Studio Live Broadcast Link</option>
                                    <option value="Discord Voice Channel">Discord Communication Driver</option>
                                </select>
                            </div>

                            <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                                <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Virtual Camera (Video Routing)</h3>
                                <p style={{ fontSize: '13px', color: '#94a3b8', marginBottom: '16px' }}>Outputs face-swapped video frames into web browsers and video call software.</p>

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
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Manage cloud synchronization for voice metadata (Firestore) and heavy binary models (Firebase Storage).</p>
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

                {/* TAB 5: HARDWARE & SETTINGS */}
                {activeTab === 'settings' && (
                    <div>
                        <header style={{ marginBottom: '24px', borderBottom: '1px solid #1e293b', paddingBottom: '16px' }}>
                            <h1 style={{ margin: 0, fontSize: '24px', color: '#38bdf8' }}>Hardware Acceleration & Performance</h1>
                            <p style={{ margin: '4px 0 0', color: '#94a3b8', fontSize: '13px' }}>Configure AI execution providers (Metal/CoreML, CUDA, DirectML, CPU) and adaptive quality tiers.</p>
                        </header>

                        <div style={{ background: '#0f172a', padding: '20px', borderRadius: '12px', border: '1px solid #1e293b', maxWidth: '600px' }}>
                            <h3 style={{ marginTop: 0, fontSize: '16px', color: '#e2e8f0' }}>Detected Hardware Profile</h3>
                            <ul style={{ fontSize: '13px', color: '#94a3b8', lineHeight: '1.6', paddingLeft: '20px' }}>
                                <li>Operating System: <strong style={{ color: '#fff' }}>macOS (x86_64 / Apple Silicon)</strong></li>
                                <li>AI Execution Provider: <strong style={{ color: '#38bdf8' }}>CPU (Multi-Threaded) / Metal CoreML Ready</strong></li>
                                <li>Active Memory Strategy: <strong style={{ color: '#34d399' }}>Zero-Allocation Ring Buffers</strong></li>
                            </ul>
                        </div>
                    </div>
                )}

                {/* LIVE TELEMETRY & IPC LOG STREAM */}
                <div style={{ marginTop: '24px', background: '#0f172a', padding: '16px', borderRadius: '12px', border: '1px solid #1e293b' }}>
                    <h3 style={{ marginTop: 0, fontSize: '14px', color: '#cbd5e1', marginBottom: '8px' }}>Live Telemetry & IPC Log Stream</h3>
                    <div style={{ background: '#020617', padding: '12px', borderRadius: '6px', height: '140px', overflowY: 'auto', fontFamily: 'monospace', fontSize: '12px', color: '#38bdf8', border: '1px solid #1e293b' }}>
                        {logs.length === 0 ? <span style={{ color: '#64748b' }}>Awaiting IPC activity...</span> : logs.map((log, index) => <div key={index}>{log}</div>)}
                    </div>
                </div>
            </main>
        </div>
    );
}

export default App;