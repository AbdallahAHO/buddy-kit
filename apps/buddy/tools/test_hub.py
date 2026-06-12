#!/usr/bin/env python3
"""
Minimal hub for the transport-http lego. The device polls GET /poll for
newline-delimited JSON and POSTs its replies to /push.

  python3 tools/test_hub.py [port]          # serve (default 8787)
  curl -d '{"cmd":"status"}' http://localhost:8787/queue   # enqueue for device

Everything the device pushes is printed to stdout.
"""
import sys, time, threading
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8787
_lock = threading.Lock()
_queue = []   # lines waiting for the device

class Hub(BaseHTTPRequestHandler):
    def log_message(self, *a): pass   # quiet the default access log

    def do_GET(self):
        if self.path != '/poll':
            self.send_response(404); self.end_headers(); return
        with _lock:
            body = ''.join(l if l.endswith('\n') else l + '\n' for l in _queue)
            _queue.clear()
        data = body.encode()
        self.send_response(200 if data else 204)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        if data: self.wfile.write(data)

    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(n).decode(errors='replace')
        if self.path == '/push':
            for line in body.splitlines():
                if line.strip():
                    print(f"[{time.strftime('%H:%M:%S')}] device → {line}", flush=True)
        elif self.path == '/queue':
            with _lock:
                _queue.extend(l for l in body.splitlines() if l.strip())
            print(f"[{time.strftime('%H:%M:%S')}] queued  ← {body.strip()}", flush=True)
        else:
            self.send_response(404); self.end_headers(); return
        self.send_response(200)
        self.send_header('Content-Length', '2')
        self.end_headers()
        self.wfile.write(b'ok')

print(f"hub on :{PORT} — point the device at http://<this-host>:{PORT}", flush=True)
HTTPServer(('0.0.0.0', PORT), Hub).serve_forever()
