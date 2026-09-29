import React, { useState } from "react";
import { EngineIPCClient } from "./services/EngineIPCClient";

export function App() {
    const [activeProfile, setActiveProfile] = useState<string>("user_custom_profile");
    const [pitchShift, setPitchShift] = useState<number>(2);
    const [callStatus, setCallStatus] = useState<string>("Disconnected");
    const [logs, setLogs] = useState<string[]>([]);

    const addLog = (message: string) => {
        setLogs((prev) => [`[${new Date().toLocaleTimeString()}] ${message}`, ...prev.slice(0, 49)]);
    };

    const handleStartCall = async () => {
        addLog(`Sending START_CALL command for profile: ${activeProfile}...`);
        const res = await EngineIPCClient.sendCommand({
            command: "START_CALL",
            profile: activeProfile,
        });
        addLog(`Engine Response: ${JSON.stringify(res)}`);
        if (res.status === "ok") {
            setCallStatus("Live (Voice Conversion Active)");
        }
    };

    const handleUpdatePitch = async (newPitch: number) => {
        setPitchShift(newPitch);
        addLog(`Sending SET_PITCH command: ${newPitch} semitones...`);
        const res = await EngineIPCClient.sendCommand({
            command: "SET_PITCH",
            pitch: newPitch,
        });
        addLog(`Engine Response: ${JSON.stringify(res)}`);
    };

    const handleStopCall = async () => {
        addLog("Sending STOP_CALL command...");
        const res = await EngineIPCClient.sendCommand({
            command: "STOP_CALL",
        });
        addLog(`Engine Response: ${JSON.stringify(res)}`);
        setCallStatus("Disconnected");
    };

    return (
        <div style={{ fontFamily: "system-ui, sans-serif", padding: "24px", maxWidth: "800px", margin: "0 auto", background: "#0f172a", color: "#f8fafc", minHeight: "100vh" }}>
            <header style={{ borderBottom: "1px solid #334155", paddingBottom: "16px", marginBottom: "24px" }}>
                <h1 style={{ margin: 0, fontSize: "24px", color: "#38bdf8" }}>AI Studio Engine - Control Dashboard</h1>
                <p style={{ margin: "4px 0 0", color: "#94a3b8", fontSize: "14px" }}>Local C++20 Core Bridge | Status: <span style={{ color: callStatus.includes("Live") ? "#4ade80" : "#f87171" }}>{callStatus}</span></p>
            </header>

            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "20px", marginBottom: "24px" }}>
                <div style={{ background: "#1e293b", padding: "16px", borderRadius: "8px", border: "1px solid #334155" }}>
                    <h3 style={{ marginTop: 0, fontSize: "16px" }}>Voice Profile Selection</h3>
                    <label style={{ display: "block", fontSize: "13px", color: "#94a3b8", marginBottom: "8px" }}>Active Profile ID</label>
                    <input
                        type="text"
                        value={activeProfile}
                        onChange={(e) => setActiveProfile(e.target.value)}
                        style={{ width: "100%", padding: "8px", background: "#0f172a", border: "1px solid #475569", color: "#fff", borderRadius: "4px", marginBottom: "16px" }}
                    />

                    <label style={{ display: "block", fontSize: "13px", color: "#94a3b8", marginBottom: "8px" }}>Pitch Shift ({pitchShift} semitones)</label>
                    <input
                        type="range"
                        min="-12"
                        max="12"
                        value={pitchShift}
                        onChange={(e) => handleUpdatePitch(parseInt(e.target.value))}
                        style={{ width: "100%" }}
                    />
                </div>

                <div style={{ background: "#1e293b", padding: "16px", borderRadius: "8px", border: "1px solid #334155", display: "flex", flexDirection: "column", justifyContent: "space-between" }}>
                    <h3 style={{ marginTop: 0, fontSize: "16px" }}>Call Operations</h3>
                    <p style={{ fontSize: "13px", color: "#94a3b8" }}>Launch or terminate real-time microphone conversion routed directly to virtual system devices.</p>
                    <div style={{ display: "flex", gap: "10px" }}>
                        <button
                            onClick={handleStartCall}
                            style={{ flex: 1, background: "#0284c7", color: "#fff", border: "none", padding: "10px", borderRadius: "4px", fontWeight: "bold", cursor: "pointer" }}
                        >
                            Start Call
                        </button>
                        <button
                            onClick={handleStopCall}
                            style={{ flex: 1, background: "#dc2626", color: "#fff", border: "none", padding: "10px", borderRadius: "4px", fontWeight: "bold", cursor: "pointer" }}
                        >
                            Stop Call
                        </button>
                    </div>
                </div>
            </div>

            <div style={{ background: "#1e293b", padding: "16px", borderRadius: "8px", border: "1px solid #334155" }}>
                <h3 style={{ marginTop: 0, fontSize: "16px", color: "#cbd5e1" }}>Live Telemetry & IPC Log Stream</h3>
                <div style={{ background: "#0f172a", padding: "12px", borderRadius: "4px", height: "180px", overflowY: "auto", fontFamily: "monospace", fontSize: "12px", color: "#38bdf8", border: "1px solid #334155" }}>
                    {logs.length === 0 ? <span style={{ color: "#64748b" }}>Awaiting IPC activity...</span> : logs.map((log, index) => <div key={index}>{log}</div>)}
                </div>
            </div>
        </div>
    );
}

export default App;