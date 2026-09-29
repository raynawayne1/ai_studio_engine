export type NavigationTab =
  | "dashboard"
  | "voices"
  | "routing"
  | "firebase"
  | "settings";

export interface VoiceProfile {
  id: string;
  displayName: string;
  sourcePath: string;
  modelPath: string;
  isCached: boolean;
  createdAt: string;
}

export interface AudioVideoDevices {
  inputMicrophones: string[];
  outputSpeakers: string[];
  virtualMicTargets: string[];
  cameras: string[];
  virtualCamTargets: string[];
}

export interface EngineSettings {
  pitchShift: number;
  indexRate: number;
  protectRate: number;
  faceSwapEnabled: boolean;
  lipSyncEnabled: boolean;
  backgroundMattingEnabled: boolean;
  garmentOverlayEnabled: boolean;
  performanceMode: "Performance" | "Balanced" | "Quality" | "Custom";
}
