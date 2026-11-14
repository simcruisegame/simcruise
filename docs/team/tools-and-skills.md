# Tools, skills and plugins: what each team uses (owner ask, 2026-11-14)

Owner: "make sure you are utilizing skills and plugins as well." Tools engineer owns this list. The **plan step** of the loop (`docs/team/dev-loop.md`) names the tools for each item.

Skills are Claude Code skills on this account (call them with the Skill tool, or `/name`). A cloud session and a PC Remote Control session can list different skills: run `/skills` (or check the Skill list) at the start of a session. Expert subagents are in `.claude/agents/` (18 roles).

## For every thread

| Task | Use |
|---|---|
| Plan a multi-step change | `zizi-skills:planning`, then the cycle file |
| Ground a change in official docs | `zizi-skills:source-driven-development`; Unreal docs via `unreal-engine-skills-for-claude-code:unreal-skill`; Windows/DirectX/PowerShell via `microsoft-docs` |
| Fix a bug | `zizi-skills:debugging-and-error-recovery` or `supermatt:diagnosing-bugs` (a red repro first, then the fix, then a regression test) |
| Write tests first | `zizi-skills:tdd-skill` / `supermatt:tdd` |
| Review before merge | `code-review` (then `simplify` for clean-up); `security-review` for helper network code, downloads and model files |
| Many files or a long job | `zizi-skills:orchestration` with the expert subagents |
| Hard design choice | `zizi-skills:adversarial-review` (only when the owner or the producer asks for multi-seat work) |
| Research with sources | `anthropic-skills:deep-research`, `supermatt:research`, `zizi-skills:research-methods` |
| Charts of test numbers | `dataviz` |
| Papers and manuals (PDF) | `anthropic-skills:pdf` |

## By team

| Team | Skills and agents | Plugins and tools on the PC | Unreal features to use (not used yet unless marked) |
|---|---|---|---|
| Production | `product-management:sprint-planning`, `product-management:roadmap-update`, `product-management:write-spec`, `zizi-skills:status-update`; agent `producer` | GitHub (gh on the PC) | — |
| Design and gameplay | `design:design-critique`, `design:ux-copy`, `design:accessibility-review`, `a11y-enforcer:contrast` (HUD contrast); agents `game-designer`, `ux-accessibility` | Figma connector for menu mock-ups (optional) | CommonUI for menus (study), Enhanced Input (in use) |
| Car team | `zizi-skills:source-driven-development` (manuals, SAE/ISO), `dataviz` (bench plots); agents `vehicle-dynamics-physicist`, `automotive-engineer` | **Blender MCP** (cockpit rig checks: wheel rim circle, dial positions), Project Chrono as a tyre reference (BSD-3) | Automation tests (in use), Chaos async physics (in use) |
| Engineering | `unreal-engine-skills-for-claude-code:unreal-skill`, `engineering:debug`, `engineering:architecture`, `engineering:tech-debt`; agents `game-developer`, `performance-engineer` | **Unreal Insights** (`-trace=default,frame`; tree refresh and install steps), `stat unit`/`stat gpu` | **Insights traces** for every hitch; World Partition streaming ideas; async tasks for tree builds |
| Picture | `unreal-skill`, `dataviz` (luma histograms); agents `lighting-director`, `cinematographer`, `technical-artist` | **Blender MCP** (car materials, rig), ffmpeg (sheets), realism.ps1 | **Runtime Virtual Texturing** for ground and road blends; Nanite (meshes); Lumen (in use); post-process local exposure (in use) |
| World | `unreal-skill`, `unreal-engine-skills-for-claude-code:unreal-mcp` (drive the editor for PCG graphs), `create-toolset`; agents `gis-data-scientist`, `environment-artist`, `magician` | **Unreal MCP** plugin (enabled in `SimCruise.uproject`: ModelContextProtocol, AllToolsets), **Blender MCP** (tree and kit meshes), Poly Haven / ambientCG (CC0) | **PCG framework + shape grammar** (buildings, roadside, City Sample 5.8 pattern); **Nanite foliage** for real tree meshes; **HLOD**; **Runtime Virtual Texturing** for terrain and road edges |
| AI (local models) | `zizi-skills:source-driven-development` (model cards and licences), `security-review` (model files and downloads); agents `research-scientist`, `applied-mathematician`, `performance-engineer` | ONNX Runtime CPU (NNERuntimeORT plugin, enabled), Python lab for experiments | **NNE** (Neural Network Engine) with the ORT CPU backend; never on the game thread |
| Innovation (research) | `anthropic-skills:deep-research`, `supermatt:prototype`, `zizi-skills:idea-refine`, `zizi-skills:research-methods`; agents `applied-mathematician`, `research-scientist`, `magician` | Python lab, Blender MCP | PCG, RVT, Nanite foliage (prototypes) |
| Audio | `source-driven-development` (recordings, papers); agent `sound-designer` | ffmpeg `ebur128` (loudness) | **MetaSounds** as the host for our C++ synths (custom nodes); Submixes and Sound Classes |
| QA and tools | `supermatt:qa`, `engineering:testing-strategy`, `engineering:deploy-checklist`, `zizi-skills:monitoring`; agents `qa-playtester`, `tools-engineer` | check.ps1, preflight.ps1 (with `-Roads random`), framecheck.ps1, replay-compare.ps1, watchdog.ps1, clean-lane.ps1 | **Gauntlet** for packaged-build tests (study); Automation (in use); Insights for frame review |

## Gaps found in the audit (2026-11-14)

1. Nobody uses Unreal Insights traces yet; hitches are found from frame-time numbers only. Owner: Engineering.
2. PCG, shape grammar, Nanite foliage and Runtime Virtual Texturing are not used; the world builds every mesh by hand in C++. Owner: World (with the scan engine).
3. MetaSounds is not used; the synths output raw buffers with no submix. Owner: Audio.
4. Gauntlet is not used, so no test runs the packaged build at a place. Owner: QA and tools.
5. The Unreal MCP plugin is enabled but no thread uses it in its workflow. Owner: World and Tools.
6. Skills for debugging, TDD and review exist on the account but threads do not name them in plans. Fix: the plan step names them (loop rule).

## Licence notes for the producer

- The 8 detail-kit textures on main were made with SDXL. The world thread replaces them with CC0 scans.
- Real-ESRGAN is in `SimCruiseNeuralDetail`. The live-data thread removes it.
- Fab and Megascans assets stay out of the public repo (see `/mnt/project-files/research/free-unreal-tools.md`).

## PC audit (2026-11-14 03:10Z)

- `D:\simcruise` is checked out on branch `unreal/client`, which does not have `.claude/agents`. A Claude session that starts there cannot see the 18 experts. Start sessions in a lane worktree on main (`D:\sc\lane-*`), or bring `.claude/agents` into that checkout.
- `unreal-mcp` (`http://127.0.0.1:8000/mcp`, in `D:\simcruise\.mcp.json`) works only while the editor runs with its plugin (start the editor with `scripts\editor.ps1`).
- The Blender MCP (`uvx blender-mcp`) connects and drops; retry it. Tools: scene info, run Blender code, look, search and import assets.
- desktop-commander fails to connect on the PC.
- On the PC these skills are visible: Unreal (unreal-mcp, unreal-skill, create-toolset), engineering (architecture, code-review, debug, testing-strategy, tech-debt), supermatt (diagnosing-bugs, tdd, qa, prototype, research), zizi-skills (debugging-and-error-recovery, planning, orchestration, research-methods, source-driven-development), built-in code-review, security-review, simplify. No project skills folder (`.claude/skills`) exists yet.
