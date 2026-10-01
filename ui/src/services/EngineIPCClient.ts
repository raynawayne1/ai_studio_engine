import { VoiceProfile } from "../types";

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
  display_name?: string;
  source_path?: string;
  pitch?: number;
  index_rate?: number;
  protect_rate?: number;
  input_gain?: number;
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
  avatar_ready?: boolean;
  active_source?: string;
  active_voice?: string;
  active_voice_name?: string;
  fps?: number;
  speech_energy?: number;
  audio_peak?: number;
  input_gain?: number;
  routed_frames?: number;
  left_arm_raised?: boolean;
  right_arm_raised?: boolean;
  hands_holding_body?: boolean;
  full_body_visible?: boolean;
  eyes_blinking?: boolean;
  head_tilt?: number;
  cloud_status?: string;
  os_name?: string;
  cpu_arch?: string;
  logical_cores?: number;
  total_ram_gb?: number;
  provider?: string;
  devices?: DiscoveredCameraDevice[];
  voices?: VoiceProfile[];
  models?: CachedModelItem[];
  data?: any;
}

export interface FrameFetchResult {
  ok: boolean;
  status: number;
  bytes: number;
  objectUrl: string;
  error?: string;
}

export class EngineIPCClient {
  public static readonly IPC_URL = "http://127.0.0.1:8765";

  private static pushedFrameCounter = 0;
  private static fetchedSourceCounter = 0;
  private static fetchedOutputCounter = 0;
  private static processedAudioCounter = 0;

  public static getSourceFrameUrl(cacheBust: number): string {
    return `${this.IPC_URL}/frame/source?t=${cacheBust}`;
  }

  public static getOutputFrameUrl(cacheBust: number): string {
    return `${this.IPC_URL}/frame/output?t=${cacheBust}`;
  }

  // Zero-Flicker Binary Frame Fetcher
  public static async fetchLiveFrame(
    kind: "source" | "output",
    timeoutMs: number = 350,
  ): Promise<FrameFetchResult> {
    const endpoint =
      kind === "source"
        ? `${this.IPC_URL}/frame/source?t=${Date.now()}`
        : `${this.IPC_URL}/frame/output?t=${Date.now()}`;

    try {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), timeoutMs);

      const res = await fetch(endpoint, {
        method: "GET",
        signal: controller.signal,
        cache: "no-store",
      });

      clearTimeout(timer);

      if (res.status === 204) {
        return { ok: false, status: 204, bytes: 0, objectUrl: "" };
      }

      if (!res.ok) {
        return {
          ok: false,
          status: res.status,
          bytes: 0,
          objectUrl: "",
          error: `HTTP ${res.status}`,
        };
      }

      const blob = await res.blob();
      if (!blob || blob.size <= 32) {
        return { ok: false, status: res.status, bytes: 0, objectUrl: "" };
      }

      const objectUrl = URL.createObjectURL(blob);
      if (kind === "source") {
        this.fetchedSourceCounter++;
        if (
          this.fetchedSourceCounter === 1 ||
          this.fetchedSourceCounter % 120 === 0
        ) {
          console.log(
            `[DEBUG][ui/src/services/EngineIPCClient.ts::fetchLiveFrame] Pulled C++ /frame/source #${this.fetchedSourceCounter} (${blob.size} bytes)`,
          );
        }
      } else {
        this.fetchedOutputCounter++;
        if (
          this.fetchedOutputCounter === 1 ||
          this.fetchedOutputCounter % 120 === 0
        ) {
          console.log(
            `[DEBUG][ui/src/services/EngineIPCClient.ts::fetchLiveFrame] Pulled C++ /frame/output #${this.fetchedOutputCounter} (${blob.size} bytes)`,
          );
        }
      }

      return {
        ok: true,
        status: 200,
        bytes: blob.size,
        objectUrl,
      };
    } catch (err: any) {
      return {
        ok: false,
        status: 0,
        bytes: 0,
        objectUrl: "",
        error: err?.message || "Frame fetch failed",
      };
    }
  }

  public static async pushRawFrameToEngine(jpegBlob: Blob): Promise<boolean> {
    try {
      const response = await fetch(`${this.IPC_URL}/frame/push`, {
        method: "POST",
        headers: {
          "Content-Type": "image/jpeg",
        },
        body: jpegBlob,
        cache: "no-store",
      });

      if (response.ok) {
        this.pushedFrameCounter++;
        if (
          this.pushedFrameCounter === 1 ||
          this.pushedFrameCounter % 120 === 0
        ) {
          console.log(
            `[DEBUG][ui/src/services/EngineIPCClient.ts::pushRawFrameToEngine] Pushed WebRTC frame #${this.pushedFrameCounter} (${jpegBlob.size} bytes) -> C++ /frame/push`,
          );
        }
      }
      return response.ok;
    } catch {
      return false;
    }
  }

  // Streams real microphone float32 PCM samples into C++ VoiceInferenceEngine and returns converted float32 samples
  public static async processLiveMicAudio(
    inputSamples: Float32Array,
  ): Promise<Float32Array | null> {
    try {
      const rawBuffer = inputSamples.buffer.slice(
        inputSamples.byteOffset,
        inputSamples.byteOffset + inputSamples.byteLength,
      ) as ArrayBuffer;

      const res = await fetch(`${this.IPC_URL}/audio/process`, {
        method: "POST",
        headers: {
          "Content-Type": "application/octet-stream",
        },
        body: rawBuffer,
        cache: "no-store",
      });

      if (!res.ok || res.status === 204) return null;
      const arrayBuf = await res.arrayBuffer();
      if (!arrayBuf || arrayBuf.byteLength === 0) return null;

      this.processedAudioCounter++;
      if (
        this.processedAudioCounter === 1 ||
        this.processedAudioCounter % 120 === 0
      ) {
        console.log(
          `[DEBUG][ui/src/services/EngineIPCClient.ts::processLiveMicAudio] Converted live mic chunk #${this.processedAudioCounter} (${arrayBuf.byteLength} bytes) via C++ VoiceInferenceEngine`,
        );
      }

      return new Float32Array(arrayBuf);
    } catch {
      return null;
    }
  }

  public static async sendCommand(
    payload: EngineCommand,
    timeoutMs: number = 2000,
  ): Promise<EngineResponse> {
    try {
      if (payload.command !== "GET_CAMERA_STATUS") {
        console.log(
          `[DEBUG][ui/src/services/EngineIPCClient.ts::sendCommand] Sending ->`,
          JSON.stringify(payload),
        );
      }

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

      const json = (await response.json()) as EngineResponse;
      if (payload.command !== "GET_CAMERA_STATUS") {
        console.log(
          `[DEBUG][ui/src/services/EngineIPCClient.ts::sendCommand] Response <-`,
          JSON.stringify(json),
        );
      }
      return json;
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
  ): Promise<EngineResponse> {
    return await this.sendCommand(
      {
        command: "START_CAMERA",
        camera_index: cameraIndex,
        index: cameraIndex,
        url: streamUrl,
        endpoint: streamUrl,
      },
      3000,
    );
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

  public static async getVoices(): Promise<EngineResponse> {
    return await this.sendCommand({ command: "GET_VOICES" }, 2000);
  }

  public static async cloneVoiceFromFile(
    sourcePath: string,
    displayName: string,
  ): Promise<EngineResponse> {
    return await this.sendCommand(
      {
        command: "CLONE_VOICE",
        source_path: sourcePath,
        display_name: displayName,
      },
      4000,
    );
  }

  public static async deleteVoiceProfile(
    profileId: string,
  ): Promise<EngineResponse> {
    return await this.sendCommand(
      {
        command: "DELETE_VOICE",
        profile: profileId,
      },
      2000,
    );
  }

  public static async setVoiceProfile(
    profileIdOrPath: string,
  ): Promise<boolean> {
    const res = await this.sendCommand({
      command: "SET_VOICE",
      profile: profileIdOrPath,
      model_path: profileIdOrPath,
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

  public static async registerModel(
    modelPath: string,
  ): Promise<EngineResponse> {
    return await this.sendCommand(
      {
        command: "REGISTER_MODEL",
        model_path: modelPath,
      },
      3000,
    );
  }

  public static async syncCloudModels(): Promise<EngineResponse> {
    return await this.sendCommand({ command: "SYNC_CLOUD" }, 2000);
  }

  public static async getHardwareProfile(): Promise<EngineResponse> {
    return await this.sendCommand({ command: "GET_HARDWARE_PROFILE" }, 1500);
  }
}
