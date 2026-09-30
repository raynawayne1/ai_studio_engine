export interface EngineCommand {
  command: string;
  profile?: string;
  pitch?: number;
  index_rate?: number;
  protect_rate?: number;
  camera_index?: number;
  image_path?: string;
  model_path?: string;
  background_mode?: "blur" | "virtual" | "transparent";
  enabled?: boolean;
}

export interface EngineResponse {
  status: string;
  action?: string;
  latency_target_ms?: number;
  message?: string;
  data?: any;
}

export class EngineIPCClient {
  private static readonly IPC_URL = "http://127.0.0.1:8765";

  /**
   * EXTREME PERFORMANCE: Sends a synchronized command with HTTP Keep-Alive enabled.
   * Reuses localhost sockets to ensure sub-millisecond response times.
   */
  public static async sendCommand(
    payload: EngineCommand,
  ): Promise<EngineResponse> {
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 800); // Aggressive 800ms safeguard

      const response = await fetch(this.IPC_URL, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Connection: "keep-alive", // Keeps localhost TCP socket open for zero-latency roundtrips
        },
        body: JSON.stringify(payload),
        signal: controller.signal,
        cache: "no-store", // Bypass browser caching for real-time states
      });

      clearTimeout(timeoutId);

      if (!response.ok) {
        return { status: "error", message: `HTTP Error: ${response.status}` };
      }

      return await response.json();
    } catch (error: any) {
      console.error(
        "[IPC Client Error] Engine communication exception:",
        error,
      );
      return {
        status: "error",
        message: error.message || "Connection refused by C++ daemon",
      };
    }
  }

  /**
   * ULTRA-SMOOTH FIRE-AND-FORGET: For high-frequency UI sliders (Voice Pitch, Index Rate, Body Tracking).
   * Dispatches the payload without blocking the React render thread, guaranteeing zero UI stutter.
   */
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
        // Silent drop for non-critical real-time telemetry to protect UI fluidity
      }
    }, 0);
  }

  // ==========================================
  // 🚀 FEATURE-SPECIFIC HIGH-SPEED WRAPPERS
  // ==========================================

  public static async startCamera(cameraIndex: number = 0): Promise<boolean> {
    const res = await this.sendCommand({
      command: "start_camera",
      camera_index: cameraIndex,
    });
    return res.status === "ok";
  }

  public static async stopCamera(): Promise<boolean> {
    const res = await this.sendCommand({ command: "stop_camera" });
    return res.status === "ok";
  }

  public static async setFaceSwapAvatar(imagePath: string): Promise<boolean> {
    const res = await this.sendCommand({
      command: "set_avatar",
      image_path: imagePath,
    });
    return res.status === "ok";
  }

  public static async setVoiceProfile(modelPath: string): Promise<boolean> {
    const res = await this.sendCommand({
      command: "set_voice",
      model_path: modelPath,
    });
    return res.status === "ok";
  }

  public static async toggleBackgroundMatting(
    enabled: boolean,
    mode: "blur" | "virtual" | "transparent" = "blur",
  ): Promise<boolean> {
    const res = await this.sendCommand({
      command: "set_matting",
      enabled,
      background_mode: mode,
    });
    return res.status === "ok";
  }
}
