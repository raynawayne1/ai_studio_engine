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
  f0Bias?: number;
  formantF1?: number;
  formantF2?: number;
  isCached: boolean;
  input_gain?: number;
  createdAt?: string;
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
  inputGain: number;
  protectRate: number;
  faceSwapEnabled: boolean;
  lipSyncEnabled: boolean;
  backgroundMattingEnabled: boolean;
  garmentOverlayEnabled: boolean;
  performanceMode: "Performance" | "Balanced" | "Quality" | "Custom";
}
