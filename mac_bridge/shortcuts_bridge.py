#!/usr/bin/env python3
"""
macOS Shortcuts HTTP Bridge for CrowPanel ESP32-P4 Face Dashboard

Run this on your Mac to allow the ESP32 to trigger macOS Shortcuts
via HTTP requests over the local network.

Usage:
    python3 shortcuts_bridge.py [--port 8765] [--bind 0.0.0.0]

The ESP32 sends requests like:
    GET http://<mac-ip>:8765/trigger?shortcut=Toggle%20Office%20Lights

This bridge invokes the shortcut via the `shortcuts` CLI tool.

For persistence, add as a launchd service or login item.
"""

import argparse
import subprocess
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs


class ShortcutsBridgeHandler(BaseHTTPRequestHandler):
    """HTTP request handler that triggers macOS Shortcuts."""

    def do_GET(self):
        parsed = urlparse(self.path)
        params = parse_qs(parsed.query)

        if parsed.path == "/trigger":
            shortcut_name = params.get("shortcut", [None])[0]
            if shortcut_name:
                self.trigger_shortcut(shortcut_name)
            else:
                self.send_error_response(400, "Missing 'shortcut' parameter")

        elif parsed.path == "/health":
            self.send_json_response(200, '{"status": "ok"}')

        elif parsed.path == "/list":
            self.list_shortcuts()

        else:
            self.send_error_response(404, "Unknown endpoint")

    def trigger_shortcut(self, name):
        """Run a macOS Shortcut by name."""
        try:
            print(f"Triggering shortcut: {name}")
            proc = subprocess.Popen(
                ["shortcuts", "run", name],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            # Don't wait for completion — shortcuts can be long-running
            self.send_json_response(200, f'{{"triggered": "{name}"}}')
        except FileNotFoundError:
            self.send_error_response(
                500, "'shortcuts' CLI not found — requires macOS 12+"
            )
        except Exception as e:
            self.send_error_response(500, str(e))

    def list_shortcuts(self):
        """List available macOS Shortcuts."""
        try:
            result = subprocess.run(
                ["shortcuts", "list"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            shortcuts = result.stdout.strip().split("\n")
            items = ", ".join(f'"{s}"' for s in shortcuts if s)
            self.send_json_response(200, f'{{"shortcuts": [{items}]}}')
        except Exception as e:
            self.send_error_response(500, str(e))

    def send_json_response(self, code, body):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body.encode())

    def send_error_response(self, code, message):
        self.send_json_response(code, f'{{"error": "{message}"}}')

    def log_message(self, format, *args):
        """Override to use a cleaner log format."""
        print(f"[{self.log_date_time_string()}] {args[0]}")


def main():
    parser = argparse.ArgumentParser(description="macOS Shortcuts HTTP Bridge")
    parser.add_argument("--port", type=int, default=8765, help="Listen port")
    parser.add_argument("--bind", default="0.0.0.0", help="Bind address")
    args = parser.parse_args()

    # Verify we're on macOS
    if sys.platform != "darwin":
        print("Warning: This bridge is designed for macOS. "
              "The 'shortcuts' CLI may not be available on this platform.")

    server = HTTPServer((args.bind, args.port), ShortcutsBridgeHandler)
    print(f"Shortcuts Bridge listening on {args.bind}:{args.port}")
    print(f"Trigger URL: http://<this-mac-ip>:{args.port}/trigger?shortcut=<name>")
    print(f"Health check: http://<this-mac-ip>:{args.port}/health")
    print(f"List shortcuts: http://<this-mac-ip>:{args.port}/list")
    print("Press Ctrl+C to stop")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()


if __name__ == "__main__":
    main()
