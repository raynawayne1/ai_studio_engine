import React from 'react';
import { NavigationTab } from '../types';

interface SidebarProps {
    activeTab: NavigationTab;
    setActiveTab: (tab: NavigationTab) => void;
    callActive: boolean;
}

export const Sidebar: React.FC<SidebarProps> = ({ activeTab, setActiveTab, callActive }) => {
    const navItems: { id: NavigationTab; label: string; icon: string }[] = [
        { id: 'dashboard', label: 'Live Studio Call', icon: '🎙️' },
        { id: 'voices', label: 'Voice Profiles & Library', icon: '🗣️' },
        { id: 'routing', label: 'Device Routing (OBS/Zoom)', icon: '🎛️' },
        { id: 'firebase', label: 'Cloud & Firebase Sync', icon: '☁️' },
        { id: 'settings', label: 'Hardware & Performance', icon: '⚙️' },
    ];

    return (
        <aside style={{ width: '260px', background: '#090d16', borderRight: '1px solid #1e293b', display: 'flex', flexDirection: 'column', height: '100vh', padding: '20px' }}>
            <div style={{ marginBottom: '30px' }}>
                <h2 style={{ fontSize: '18px', color: '#38bdf8', margin: '0 0 4px 0' }}>AI Studio Pro</h2>
                <p style={{ fontSize: '11px', color: '#64748b', margin: 0 }}>Real-Time Deepfake Engine</p>
            </div>

            <nav style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: '8px' }}>
                {navItems.map((item) => {
                    const isActive = activeTab === item.id;
                    return (
                        <button
                            key={item.id}
                            onClick={() => setActiveTab(item.id)}
                            style={{
                                display: 'flex',
                                alignItems: 'center',
                                gap: '12px',
                                width: '100%',
                                padding: '12px 16px',
                                background: isActive ? '#1e293b' : 'transparent',
                                color: isActive ? '#38bdf8' : '#94a3b8',
                                border: '1px solid',
                                borderColor: isActive ? '#334155' : 'transparent',
                                borderRadius: '8px',
                                cursor: 'pointer',
                                fontSize: '14px',
                                fontWeight: isActive ? 600 : 400,
                                textAlign: 'left',
                                transition: 'all 0.2s ease',
                            }}
                        >
                            <span>{item.icon}</span>
                            <span>{item.label}</span>
                        </button>
                    );
                })}
            </nav>

            <div style={{ padding: '12px', background: '#0f172a', borderRadius: '8px', border: '1px solid #1e293b' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '4px' }}>
                    <div style={{ width: '8px', height: '8px', borderRadius: '50%', background: callActive ? '#22c55e' : '#ef4444' }} />
                    <span style={{ fontSize: '12px', fontWeight: 600, color: '#f8fafc' }}>
                        {callActive ? 'Call Live (Routing Active)' : 'Engine Idle'}
                    </span>
                </div>
                <p style={{ fontSize: '10px', color: '#64748b', margin: 0 }}>C++20 Core Bridge Connected</p>
            </div>
        </aside>
    );
};