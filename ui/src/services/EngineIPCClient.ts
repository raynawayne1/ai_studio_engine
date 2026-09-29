export interface EngineCommand {
  command: string;
  profile?: string;
  pitch?: number;
  index_rate?: number;
  protect_rate?: number;
}

export interface EngineResponse {
  status: string;
  action?: string;
  latency_target_ms?: number;
  message?: string;
}

export class EngineIPCClient {
  private static readonly IPC_URL = "http://127.0.0.1:8765";

  public static async sendCommand(
    payload: EngineCommand,
  ): Promise<EngineResponse> {
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 2000); // 2s timeout safeguard

      const response = await fetch(this.IPC_URL, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify(payload),
        signal: controller.signal,
      });

      clearTimeout(timeoutId);

      if (!response.ok) {
        return { status: "error", message: `HTTP Error: ${response.status}` };
      }

      const data: EngineResponse = await response.json();
      return data;
    } catch (error: any) {
      console.error(
        "[IPC Client Error] Failed to communicate with C++ Engine:",
        error,
      );
      return {
        status: "error",
        message: error.message || "Connection refused",
      };
    }
  }
}
