import type { DurableObjectState } from "@cloudflare/workers-types";

// One DeviceHub per device — holds that device's pending command queue
// (newline-delimited JSON lines the device drains on its next poll). The
// queue is the only realtime state; the registry/presence lives in D1.
export class DeviceHub {
  constructor(private ctx: DurableObjectState) {}

  async fetch(req: Request): Promise<Response> {
    const url = new URL(req.url);

    // Device poll: hand over everything queued, then clear.
    if (url.pathname === "/drain") {
      const q = (await this.ctx.storage.get<string[]>("q")) ?? [];
      if (q.length) await this.ctx.storage.put("q", []);
      return new Response(q.join(""), {
        headers: { "content-type": "application/x-ndjson" },
      });
    }

    // Admin/system enqueue: append one line (commands to the device).
    if (url.pathname === "/enqueue") {
      let line = await req.text();
      if (!line.endsWith("\n")) line += "\n";
      const q = (await this.ctx.storage.get<string[]>("q")) ?? [];
      q.push(line);
      while (q.length > 50) q.shift(); // cap — a device that never polls
      await this.ctx.storage.put("q", q);
      return new Response("ok");
    }

    return new Response("not found", { status: 404 });
  }
}
