import React from 'react';
import { NavigationTab } from '../types';

interface SidebarProps {
    activeTab: NavigationTab;
    setActiveTab: (tab: NavigationTab) => void;
    callActive: boolean;
    activeSource?: string;
    activeVoiceName?: string;
    fps?: number;
}

export const Sidebar: React.FC<SidebarProps> = ({
    activeTab,
    setActiveTab,
    callActive,
    activeSource,
    activeVoiceName,
    fps
}) => {
    const navItems: { id: NavigationTab; label: string; icon: string }[] = [
        { id: 'dashboard', label: 'Live Studio Call', icon: '🎥' },
        { id: 'voices', label: 'Voice Cloning Studio', icon: '🎙️' },
        { id: 'routing', label: 'Device Routing (OBS/Zoom)', icon: '🎛️' },
        { id: 'firebase', label: 'Cloud & ONNX Models', icon: '☁️' },
        { id: 'settings', label: 'Hardware & Performance', icon: '⚙️' },
    ];

    return (
        <aside
            style={{
                width: '235px',
                background: '#090d16',
                borderRight: '1px solid #1e293b',
                display: 'flex',
                flexDirection: 'column',
                height: '100vh',
                padding: '18px 14px',
                flexShrink: 0
            }}
        >
            <div style={{ marginBottom: '22px', paddingLeft: '6px' }}>
                <h2 style={{ fontSize: '18px', color: '#38bdf8', margin: '0 0 4px 0', fontWeight: 800 }}>
                    AI Studio Pro
                </h2>
                <p style={{ fontSize: '11px', color: '#64748b', margin: 0 }}>
                    C++20 Deep-Live-Cam Suite
                </p>
            </div>

            <nav style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: '6px' }}>
                {navItems.map((item) => {
                    const isActive = activeTab === item.id;
                    return (
                        <button
                            key={item.id}
                            onClick={() => setActiveTab(item.id)}
                            style={{
                                display: 'flex',
                                alignItems: 'center',
                                gap: '10px',
                                width: '100%',
                                padding: '11px 13px',
                                background: isActive ? '#1e293b' : 'transparent',
                                color: isActive ? '#38bdf8' : '#94a3b8',
                                border: '1px solid',
                                borderColor: isActive ? '#38bdf8' : 'transparent',
                                borderRadius: '8px',
                                cursor: 'pointer',
                                fontSize: '13px',
                                fontWeight: isActive ? 700 : 500,
                                textAlign: 'left',
                                transition: 'all 0.15s ease',
                            }}
                        >
                            <span>{item.icon}</span>
                            <span>{item.label}</span>
                        </button>
                    );
                })}
            </nav>

            <div
                style={{
                    padding: '12px',
                    background: '#0f172a',
                    borderRadius: '10px',
                    border: callActive ? '1px solid #22c55e' : '1px solid #1e293b',
                    display: 'flex',
                    flexDirection: 'column',
                    gap: '5px'
                }}
            >
                <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                        <div
                            style={{
                                width: '8px',
                                height: '8px',
                                borderRadius: '50%',
                                background: callActive ? '#22c55e' : '#ef4444'
                            }}
                        />
                        <span style={{ fontSize: '12px', fontWeight: 700, color: '#f8fafc' }}>
                            {callActive ? 'LIVE ON AIR' : 'Engine Standby'}
                        </span>
                    </div>
                    {callActive && typeof fps === 'number' && fps > 0 && (
                        <span style={{ fontSize: '11px', fontWeight: 700, color: '#22c55e' }}>
                            {fps.toFixed(0)} FPS
                        </span>
                    )}
                </div>
                <div style={{ fontSize: '10px', color: '#94a3b8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                    📷 {activeSource || 'Ready'}
                </div>
                <div style={{ fontSize: '10px', color: '#38bdf8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                    🎙️ {activeVoiceName || 'No Cloned Voice Yet'}
                </div>
            </div>
        </aside>
    );
};