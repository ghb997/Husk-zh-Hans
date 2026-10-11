#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Serve a folder to Husk's Downloads tab over the local network.

    python3 tools/serve-folder.py <folder> [port]

Husk is given the address this prints. GET / is the manifest, a JSON list of every file under the folder with its size;
GET /f/<path> is one file, with Range requests honoured so an interrupted download resumes where it stopped. Husk puts the
files in its Shared Storage folder at the same paths, so serve the folder whose contents belong at the top of /sdcard.
"""
import json
import os
import socket
import sys
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.realpath(sys.argv[1] if len(sys.argv) > 1 else ".")
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8642


def manifest():
    files = []
    for base, dirs, names in os.walk(ROOT):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for n in sorted(names):
            if n.startswith("."):
                continue
            full = os.path.join(base, n)
            files.append({"path": os.path.relpath(full, ROOT).replace(os.sep, "/"), "size": os.path.getsize(full)})
    return {"name": os.path.basename(ROOT), "files": files}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    def do_HEAD(self):
        self.do_GET(head=True)

    def do_GET(self, head=False):
        path = urllib.parse.unquote(urllib.parse.urlparse(self.path).path)
        if path in ("/", "/manifest.json"):
            body = json.dumps(manifest()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if not head:
                self.wfile.write(body)
            return
        if not path.startswith("/f/"):
            self.send_error(404)
            return
        full = os.path.realpath(os.path.join(ROOT, path[3:]))
        if not full.startswith(ROOT + os.sep) or not os.path.isfile(full):
            self.send_error(404)
            return
        size = os.path.getsize(full)
        start, end = 0, size - 1
        rng = self.headers.get("Range")
        if rng and rng.startswith("bytes="):
            a, _, b = rng[6:].split(",")[0].partition("-")
            if a:
                start = int(a)
                end = int(b) if b else size - 1
            elif b:
                start = max(0, size - int(b))
            if start >= size or start > end:
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % size)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            end = min(end, size - 1)
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        else:
            self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        if head:
            return
        self.wfile.flush()
        t0, sent = time.time(), 0
        with open(full, "rb") as f:
            # sendfile: the kernel copies from the file to the socket, without the data passing through Python.
            sock = self.connection
            offset, left = start, end - start + 1
            try:
                while left > 0:
                    n = os.sendfile(sock.fileno(), f.fileno(), offset, min(left, 64 << 20))
                    if n == 0:
                        break
                    offset += n
                    left -= n
                    sent += n
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
        dt = max(time.time() - t0, 0.001)
        sys.stderr.write("  %s: %.0f MB in %.0f s, %.1f MB/s\n" % (path[3:], sent / 1e6, dt, sent / 1e6 / dt))


def lan_address():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))   # no packet is sent; this only picks the interface a LAN route would use
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


if __name__ == "__main__":
    m = manifest()
    total = sum(f["size"] for f in m["files"])
    print("Serving %s: %d files, %.1f GB" % (ROOT, len(m["files"]), total / 1e9))
    print("In Husk, Downloads > Add, enter:  http://%s:%d/" % (lan_address(), PORT))
    class Server(ThreadingHTTPServer):
        daemon_threads = True
        def server_bind(self):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 << 20)
            super().server_bind()
    Server(("0.0.0.0", PORT), Handler).serve_forever()
