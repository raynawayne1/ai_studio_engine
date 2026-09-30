export interface DiscoveredCameraDevice {
  index: number;
  name: string;
  endpoint: string;
  is_virtual: boolean;
  is_stream: boolean;
}

export interface CachedModelItem {
  id: string;
  path: string;
  size_bytes: number;
}

export interface EngineCommand {
  command: string;
  profile?: string;
  pitch?: number;
  index_rate?: number;
  protect_rate?: number;
  camera_index?: number;
  index?: number;
  url?: string;
  endpoint?: string;
  image_path?: string;
  model_path?: string;
  background_mode?: "blur" | "virtual" | "transparent" | "disabled";
  enabled?: boolean;
  face_swap?: boolean;
  lip_sync?: boolean;
  body_tracking?: boolean;
  garment_overlay?: boolean;
}

export interface EngineResponse {
  status: string;
  action?: string;
  latency_target_ms?: number;
  message?: string;
  running?: boolean;
  call_active?: boolean;
  active_source?: string;
  fps?: number;
  speech_energy?: number;
  audio_peak?: number;
  routed_frames?: number;
  left_arm_raised?: boolean;
  right_arm_raised?: boolean;
  hands_holding_body?: boolean;
  full_body_visible?: boolean;
  head_tilt?: number;
  cloud_status?: string;
  devices?: DiscoveredCameraDevice[];
  models?: CachedModelItem[];
  data?: any;
}

export class EngineIPCClient {
  private static readonly IPC_URL = "http://127.0.0.1:8765";

  public static async sendCommand(
    payload: EngineCommand,
    timeoutMs: number = 1500,
  ): Promise<EngineResponse> {
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

      const response = await fetch(this.IPC_URL, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Connection: "keep-alive",
        },
        body: JSON.stringify(payload),
        signal: controller.signal,
        cache: "no-store",
      });

      clearTimeout(timeoutId);

      if (!response.ok) {
        return { status: "error", message: `HTTP Error: ${response.status}` };
      }

      return await response.json();
    } catch (error: any) {
      return {
        status: "error",
        message: error.message || "Connection refused by C++ daemon",
      };
    }
  }

  public static sendAsyncCommand(payload: EngineCommand): void {
    setTimeout(async () => {
      try {
        await fetch(this.IPC_URL, {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
            Connection: "keep-alive",
          },
          body: JSON.stringify(payload),
          cache: "no-store",
        });
      } catch {
        // Silent drop for real-time slider fluidity
      }
    }, 0);
  }

  public static async scanCameras(): Promise<EngineResponse> {
    return await this.sendCommand({ command: "SCAN_CAMERAS" }, 2500);
  }

  public static async startCamera(
    cameraIndex: number = -1,
    streamUrl: string = "",
  ): Promise<boolean> {
    const res = await this.sendCommand(
      {
        command: "START_CAMERA",
        camera_index: cameraIndex,
        index: cameraIndex,
        url: streamUrl,
        endpoint: streamUrl,
      },
      3000,
    );
    return res.status === "ok";
  }

  public static async connectUsbOrStream(
    streamUrl: string,
    cameraIndex: number = -1,
  ): Promise<EngineResponse> {
    return await this.sendCommand(
      {
        command: "CONNECT_USB",
        url: streamUrl,
        endpoint: streamUrl,
        index: cameraIndex,
        camera_index: cameraIndex,
      },
      3000,
    );
  }

  public static async getCameraStatus(): Promise<EngineResponse> {
    return await this.sendCommand({ command: "GET_CAMERA_STATUS" }, 600);
  }

  public static async stopCamera(): Promise<boolean> {
    const res = await this.sendCommand({ command: "STOP_CAMERA" });
    return res.status === "ok";
  }

  public static async setFaceSwapAvatar(imagePath: string): Promise<boolean> {
    const res = await this.sendCommand({
      command: "SET_AVATAR",
      image_path: imagePath,
    });
    return res.status === "ok";
  }

  public static async setVoiceProfile(modelPath: string): Promise<boolean> {
    const res = await this.sendCommand({
      command: "SET_VOICE",
      model_path: modelPath,
    });
    return res.status === "ok";
  }

  public static async toggleBackgroundMatting(
    enabled: boolean,
    mode: "blur" | "virtual" | "transparent" | "disabled" = "blur",
    imagePath: string = "",
  ): Promise<boolean> {
    const res = await this.sendCommand({
      command: "SET_MATTING",
      enabled,
      background_mode: mode,
      image_path: imagePath,
    });
    return res.status === "ok";
  }

  public static async syncCloudModels(): Promise<EngineResponse> {
    return await this.sendCommand({ command: "SYNC_CLOUD" }, 2000);
  }
}
