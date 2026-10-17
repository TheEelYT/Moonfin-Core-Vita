#!/usr/bin/env python3
"""Exercise the compiled client over real local HTTP without a personal server."""
import http.server
import json
import subprocess
import sys
import threading

class Server(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def send_json(self, value):
        payload = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/base/System/Info/Public":
            self.send_json({"ServerName": "Test server", "Id": "server"})
        elif self.path == "/base/too-large":
            self.send_response(200)
            self.end_headers()
            try:
                self.wfile.write(b"x" * (9 * 1024 * 1024))
            except (BrokenPipeError, ConnectionResetError):
                pass
        elif self.path == "/base/redirect":
            self.send_response(302)
            self.send_header("Location", "/base/System/Info/Public")
            self.end_headers()
        else:
            self.send_error(404)

    def do_POST(self):
        assert self.path == "/base/Users/AuthenticateByName"
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert body == {"Username": "Gene", "Pw": "test-password"}
        assert 'Client="Moonfin Vita"' in self.headers["Authorization"]
        self.send_json({"AccessToken": "test-token", "User": {"Id": "test-user", "Name": "Gene"}})

server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Server)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}/base"], check=True)
finally:
    server.shutdown()
