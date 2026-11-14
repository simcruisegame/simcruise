# Release checklist

Use this list for every SimCruise release (every release pushed to `main` and tagged). The rule: **a release is a package that people can download and play.** A release is not done until every box is ticked or the release notes name the open item.

Copy this list into the release's report (`docs/release/v<version>-report.md`) and tick it there.

## 1. Gate

- [ ] The driving checklist for the release is complete (for 2.0.0: `docs/release/driving-checklist.md`): every row PASS, or KNOWN with the reason in the release notes.
- [ ] `scripts\check.ps1` passes on the release commit (all `SimCruise.*` tests).
- [ ] `scripts\drive-robot.ps1` passes (no judged spin for any car).
- [ ] `docs/release/bugs.md` lists every bug found, with its owner and status.

## 2. Version and notes

- [ ] `ProjectVersion` in `Config/DefaultGame.ini` is the new version.
- [ ] `CHANGELOG.md`: the `Unreleased` section is moved under `## [<version>] - <YYYY-MM-DD>`, with the known issues.

## 3. Package

- [ ] `powershell -ExecutionPolicy Bypass -File scripts\release.ps1` ends with `RELEASE PACKAGE OK`. It:
  - builds `tools\atlas.exe` from `helper\atlas.mjs` (Bun),
  - packages Shipping with `scripts\package.ps1 -Configuration Shipping -Zip`,
  - builds the installer with Inno Setup 6 (`packaging\windows\simcruise.iss`, `scripts\installer.ps1`),
  - writes `SimCruise-<version>-SHA256SUMS.txt`,
  - runs the packaged game's self-test (`-SimCruiseShellSelfTest`).
- [ ] The zip has no `.pdb` files and no `Saved` folder.

## 4. Installer test (on the release PC, as a normal user)

- [ ] Run the setup; choose "Install for me only". It installs without administrator rights.
- [ ] Start SimCruise from the Start menu. The car drives (pad and keyboard), the camera changes, the sound plays.
- [ ] Quit, start again: the settings are kept.
- [ ] Uninstall from Settings > Apps. The program folder and the Start menu entry are gone.
- [ ] Write the result in `docs/release/evidence/installer.txt`.

## 5. Screenshots and docs

- [ ] 6 to 10 in-game screenshots from the Shipping build of this release in `docs/images/release-<version>/` (the cars, the cockpit, day and night, a town and a rural road).
- [ ] `README.md`: the screenshots, what is new, the download link.
- [ ] `docs/install.md`: system requirements and install steps are current.
- [ ] `docs/controls.md`: every binding is current (the `SimCruise.Gameplay.Input.BindingsMatchControlsDoc` test checks the table).

## 6. Publish

- [ ] Merge the release branch to `main` and push.
- [ ] Tag: `git tag v<version>` and `git push origin v<version>`.
- [ ] `gh release create v<version> --title "SimCruise <version>" --notes-file <notes>` with the setup, the zip, the checksums and the screenshots attached. Use `--prerelease` when any gate row is not PASS.
- [ ] Download the setup from the release page and check its SHA-256 against the checksums file.
- [ ] Write `docs/release/v<version>-report.md`: the gate results with numbers, the bugs fixed, the bugs routed, the release URL, the file sizes and the open items for the next release.

## 7. Findability (people must find the game on Google and GitHub)

- [ ] Release notes start with one plain sentence that says what the game is, with the search words: "SimCruise <version> is a free, open-source driving simulator for Windows: drive the real roads anywhere in the US, made in Unreal Engine 5."
- [ ] Website `docs/index.html` (GitHub Pages, https://simcruisegame.github.io/simcruise/): change the version in the hero line and in `softwareVersion` (JSON-LD), change the screenshots if new ones are better, and set `<lastmod>` in `docs/sitemap.xml` to the release date. Keep the facts true: do not claim a feature that the release does not have.
- [ ] `README.md` and `docs/index.html` say the same things (platforms, cars, famous roads, questions). The famous roads come from `Source/SimCruise/Private/Gameplay/SimCruiseFamousRoads.cpp`.
- [ ] Every screenshot has alt text that says the place, the car and the view.
- [ ] Repository description and topics are current: `gh repo view simcruisegame/simcruise --json description,homepageUrl,repositoryTopics`. The homepage is the website. Remove a topic that is no longer true.
- [ ] `docs/images/social-preview.png` (1280 x 640) still shows the game as it is; if you change it, ask Aaron to upload it again in Settings > General > Social preview.
- [ ] `CREDITS.md`: each new model, data source, car, texture, font or library has its maker, licence and link.
- [ ] After the push, open the website and check that the page, the images and the download link work. Check GitHub search: `gh search repos "driving simulator" --limit 50 --json fullName` and write the rank in the release report.
