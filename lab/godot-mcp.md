# Godot MCP development check

> Former name: this study was written when SimCruise was called "Sunday Driver"; the app, paths and commands it mentions under that name are now SimCruise.

The MIT-licensed Coding-Solo community server connects a local development client to an installed Godot executable over stdio. The open-source game runs independently of MCP. The upstream README and source are at https://github.com/Coding-Solo/godot-mcp/tree/1209744fad78f3998f98c7394fd0f6ef50da5281 .

On this Apple Silicon Mac, a temporary npm prefix and a temporary npm cache held `@coding-solo/godot-mcp@0.1.1`. Install scripts were disabled. No global packages, editor plugin or user-wide MCP registration were used. The server advertised version 0.1.0 in its MCP handshake although the published package is 0.1.1.

To reproduce, create a disposable directory containing a copy of only `project.godot`. Install the pinned npm package with `npm install --ignore-scripts --no-audit --no-fund --prefix /tmp/godot-mcp-check --cache /tmp/godot-mcp-check/npm-cache @coding-solo/godot-mcp@0.1.1`. Point `GODOT_PATH` at the local Godot binary. Run `node /tmp/godot-mcp-check/node_modules/@coding-solo/godot-mcp/build/index.js` in an MCP stdio client, then issue the following calls:

1. Initialize with protocol version `2024-11-05`, client name `sunday-driver-qa`, version `1` and an empty capabilities object. Send the `notifications/initialized` notification.
2. Call `tools/list`. The pinned server returned fourteen tools. They include launching the editor, running or stopping a game, reading its console, creating scenes, adding nodes, saving scenes, and querying resource UIDs.
3. Call `tools/call` for `get_godot_version` with `{}`. This machine returned `4.7.2.stable.official.ed1daf0bf`.
4. Call `tools/call` for `get_project_info` with `{"projectPath":"/absolute/path/to/disposable-project"}`. The single-file test returned the directory basename as the name, the installed Godot version, zero scenes, zero scripts, zero assets and one other file. The project directory still contained only the copied file afterward.

The server's process runs with the developer's existing file permissions. Scope its client to a disposable checkout if enabling editing or launch calls. The verified calls exercised version detection and project inspection. GPU frame timing, screenshots, vehicle dynamics, and registration of a street photograph require the game's separate diagnostics. Godot's profiler documents its frame and physics measurements at https://docs.godotengine.org/en/stable/tutorials/scripting/debug/the_profiler.html .
