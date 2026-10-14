# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender startup script: enable the MCP for Blender add-on and start its
server (localhost:9876), so Claude can drive this Blender with no clicks.

    /Applications/Blender.app/Contents/MacOS/Blender --python tools/fighter/blender_start_mcp.py

Telemetry stays off (the add-on's consent preference is left false). See
.claude/skills/make-fighter/tools.md.
"""
import addon_utils
import bpy

addon_utils.enable("blender_mcp", default_set=True, persistent=True)
bpy.ops.wm.save_userpref()


def _start():
    try:
        server = getattr(bpy.types, "blendermcp_server", None)
        if server is None or not server.running:
            bpy.ops.blendermcp.start_server()
        print("blender_start_mcp: server running on localhost:9876")
    except Exception as e:  # retried until the UI context exists
        print(f"blender_start_mcp: not yet ({e}), retrying")
        return 1.0
    return None


bpy.app.timers.register(_start, first_interval=1.0)
