#!/usr/bin/env python3
"""Standalone native transport integration test; does not run the FlatL VM."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading
import tomllib

ROOT = Path(__file__).resolve().parents[2]
CASES = tomllib.loads((ROOT / 'test/http/mock_cases.toml').read_text())['http']['mock']
BY_PATH = {item['path']: item for item in CASES}

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        case = BY_PATH.get(self.path)
        if case is None:
            self.send_error(404)
            return
        self.send_response(case['status'])
        for key, values in case['headers'].items():
            for value in values:
                self.send_header(key, value)
        self.end_headers()
        self.wfile.write(case['body'].encode())

    def log_message(self, *args):
        pass

with tempfile.TemporaryDirectory() as directory:
    binary = str(Path(directory) / 'http_probe')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-I' + str(ROOT / 'include'),
                    str(ROOT / 'src/http/flat_http_curl.c'),
                    str(ROOT / 'test/http/transport_probe.c'), '-lcurl', '-o', binary], check=True)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        for case in CASES:
            result = subprocess.run([binary, f'http://127.0.0.1:{server.server_port}{case["path"]}',
                                     str(case['status']), case['body']], capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f'{case["name"]}: {result.stdout} {result.stderr}')
            print(f'PASS {case["name"]}')
    finally:
        server.shutdown()
