# Building SimCruise from source

You need Godot 4.7.2 stable for macOS with its matching export templates ([release page](https://github.com/godotengine/godot-builds/releases/tag/4.7.2-stable)), native Apple Silicon [Bun](https://bun.com/docs/installation) (tested with 1.4.3) and Apple's Command Line Tools (`xcode-select --install`) for `codesign` and `lipo`. From a clone:

```sh
sh scripts/build-macos.sh editor   # installs locked JS dependencies, compiles tools/atlas, imports the Godot project
sh scripts/build-macos.sh app      # exports, thins to arm64, adds the helper and credits, signs ad hoc → build/SimCruise.app
sh scripts/build-macos.sh dmg 1.1.0   # the same, then packages build/SimCruise-1.1.0.dmg
sh scripts/make-dmg.sh [version] [app] [out-dir]   # package an existing build/SimCruise.app on its own
```

After `editor`, open `game/project.godot` in Godot and press F5 (the editor build runs the helper from `tools/atlas`, which is a gitignored build product). Set `GODOT_BIN` and `BUN_BIN` if the executables are not on your PATH; the script also looks in `/Applications/Godot.app`. The export uses Godot's standard macOS preset and the official universal template, then keeps only the arm64 slice. `make-dmg.sh` takes the version from its first argument or `SIMCRUISE_VERSION`, else from `application/short_version` in `game/export_presets.cfg` or `config/version` in `game/project.godot` when one is set, else from the top entry of `CHANGELOG.md` with a warning; it uses only `hdiutil`, `ditto` and `codesign`.

Headless checks run without a window and are how changes are measured; the four calibration checks and the safety rules for running Godot from scripts are in [CONTRIBUTING.md](../CONTRIBUTING.md). The original, more detailed build and diagnostics notes are kept in [docs/architecture-notes.md](architecture-notes.md).
