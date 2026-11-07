import json
import subprocess
import sys
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
EXAMPLES = ROOT / "examples"
BINARY = ROOT / "build" / "minicc"
HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
MAX_BODY = 256 * 1024
TIMEOUT_SECONDS = 10
MAX_STEPS = 200_000_000
STATIC = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/style.css": ("style.css", "text/css; charset=utf-8"),
    "/d3.min.js": ("d3.min.js", "text/javascript; charset=utf-8"),
}
ALLOWED_HOSTS = {f"127.0.0.1:{PORT}", f"localhost:{PORT}"}


def compile_source(source, stdin_text, execute):
    with tempfile.NamedTemporaryFile("w", suffix=".c", encoding="utf-8") as tmp:
        tmp.write(source)
        tmp.flush()
        command = [str(BINARY), "--json", "--max-steps", str(MAX_STEPS)]
        if not execute:
            command.append("--no-exec")
        command.append(tmp.name)
        try:
            result = subprocess.run(command, input=stdin_text.encode("utf-8"), capture_output=True, timeout=TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            return {"fatal": f"виконання перервано після {TIMEOUT_SECONDS} с"}
    if result.returncode != 0:
        return {"fatal": f"транслятор завершився з кодом {result.returncode}: {result.stderr.decode('utf-8', 'replace')}"}
    return json.loads(result.stdout.decode("utf-8", "replace"))


def load_examples():
    return [{"name": path.stem, "source": path.read_text(encoding="utf-8")} for path in sorted(EXAMPLES.glob("*.c"))]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def respond(self, status, body, content_type):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def respond_json(self, status, value):
        self.respond(status, json.dumps(value, ensure_ascii=False).encode("utf-8"), "application/json; charset=utf-8")

    def host_allowed(self):
        return self.headers.get("Host", "") in ALLOWED_HOSTS

    def do_GET(self):
        if not self.host_allowed():
            self.respond_json(403, {"fatal": "заборонений Host"})
            return
        path = urlparse(self.path).path
        if path == "/api/examples":
            self.respond_json(200, load_examples())
            return
        entry = STATIC.get(path)
        if not entry:
            self.respond_json(404, {"fatal": "не знайдено"})
            return
        self.respond(200, (WEB / entry[0]).read_bytes(), entry[1])

    def do_POST(self):
        if not self.host_allowed():
            self.respond_json(403, {"fatal": "заборонений Host"})
            return
        origin = self.headers.get("Origin")
        if origin and urlparse(origin).netloc not in ALLOWED_HOSTS:
            self.respond_json(403, {"fatal": "заборонений Origin"})
            return
        if urlparse(self.path).path != "/api/compile":
            self.respond_json(404, {"fatal": "не знайдено"})
            return
        if not self.headers.get("Content-Type", "").startswith("application/json"):
            self.respond_json(415, {"fatal": "очікується application/json"})
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_BODY:
            self.respond_json(413, {"fatal": "завеликий запит"})
            return
        try:
            payload = json.loads(self.rfile.read(length))
            source = payload["source"]
            stdin_text = payload.get("stdin", "")
            execute = bool(payload.get("execute", False))
            if not isinstance(source, str) or not isinstance(stdin_text, str):
                raise ValueError
        except (ValueError, KeyError, TypeError):
            self.respond_json(400, {"fatal": "некоректний запит"})
            return
        self.respond_json(200, compile_source(source, stdin_text, execute))


def main():
    if not BINARY.exists():
        sys.exit("спершу зберіть транслятор: make")
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"http://{HOST}:{PORT}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


main()
