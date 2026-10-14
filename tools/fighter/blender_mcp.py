#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Talk to a running MCP for Blender add-on directly over its socket
(localhost:9876, JSON), without an MCP client. Same commands the MCP tools
use: ping, get_scene_info, execute_code, get_viewport_screenshot, ...

    python3 tools/fighter/blender_mcp.py ping
    python3 tools/fighter/blender_mcp.py code path/to/script.py   # runs it in Blender, prints the result
    python3 tools/fighter/blender_mcp.py code -c "import bpy; print(len(bpy.data.objects))"
    python3 tools/fighter/blender_mcp.py screenshot out.png [max_size]
    python3 tools/fighter/blender_mcp.py raw '{"type": "get_scene_info", "params": {}}'

As a module: `from blender_mcp import send; send("execute_code", code=...)`.
"""
import json
import socket
import sys

HOST, PORT = "localhost", 9876


def send(cmd_type, timeout=600, **params):
    with socket.create_connection((HOST, PORT), timeout=timeout) as s:
        s.sendall(json.dumps({"type": cmd_type, "params": params}).encode())
        buf = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
            try:
                reply = json.loads(buf.decode())
                break
            except json.JSONDecodeError:
                continue
    if reply.get("status") == "error":
        raise RuntimeError(reply.get("message") or reply)
    return reply.get("result", reply)


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    cmd, rest = argv[0], argv[1:]
    if cmd == "ping":
        print(send("ping"))
    elif cmd == "code":
        code = rest[1] if rest[:1] == ["-c"] else open(rest[0]).read()
        r = send("execute_code", code=code)
        print(r.get("result", r) if isinstance(r, dict) else r)
    elif cmd == "screenshot":
        size = int(rest[1]) if len(rest) > 1 else 1024
        print(send("get_viewport_screenshot", filepath=rest[0], max_size=size))
    elif cmd == "raw":
        m = json.loads(rest[0])
        print(json.dumps(send(m["type"], **m.get("params", {})), indent=1))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
