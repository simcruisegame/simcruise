# Runbook: rename the GitHub repository to `osds`

Approved by Aaron in the project thread on 14 November 2026. Do it only when the coordinator says (after v2.0.3 is out), on the PC, with `gh` signed in as an admin of `simcruisegame`. It takes about 10 minutes.

What changes: `github.com/simcruisegame/simcruise` becomes `github.com/simcruisegame/osds`, and the website moves from `simcruisegame.github.io/simcruise/` to `simcruisegame.github.io/osds/`. The organisation name stays.

GitHub redirects the old repository address (web, `git clone/fetch/push`, API, release downloads). It does **not** redirect the website. **Never create a new repository named `simcruise`**: that removes GitHub's redirect for good.

## 0. Before

- PR #15 (`brand/osds`) is merged, and the link branch `brand/osds-links` is rebased on main and passes `scripts\check.ps1 -Tier fast` (it changes only text, so the fast check is a formality).
- No lane is in the middle of a push (ask the coordinator).

## 1. The redirect site for the old address

```powershell
gh repo create simcruisegame/simcruisegame.github.io --public --description "Redirects for OSDS (formerly SimCruise)"
$T = Join-Path $env:TEMP 'osds-pages'; git clone https://github.com/simcruisegame/simcruisegame.github.io.git $T
Copy-Item -Recurse D:\sc\lane-brand\authoring\brand\pages-redirect\* $T
git -C $T add -A; git -C $T commit -m "Redirect the old SimCruise website to OSDS"; git -C $T push origin HEAD:main
gh api -X POST repos/simcruisegame/simcruisegame.github.io/pages -f "source[branch]=main" -f "source[path]=/"
```

While the project repository is still named `simcruise`, it keeps serving `/simcruise/`, so this step changes nothing for visitors yet.

## 2. The rename

```powershell
gh repo rename osds --repo simcruisegame/simcruise --yes
gh repo edit simcruisegame/osds --homepage https://simcruisegame.github.io/osds/ --description "OSDS (Open Source Driving Simulator): free, open-source driving simulator for Windows. Type any US address and drive the real roads there, built live from OpenStreetMap, USGS elevation and aerial photos. Realistic car physics, traffic, GPS, wheel and Xbox controller support. Made in Unreal Engine 5."
gh api repos/simcruisegame/osds/pages --jq .html_url     # must say https://simcruisegame.github.io/osds/
```

Topics, stars, issues, pull requests and releases move with the repository. Add the topic `osds` if wanted: `gh repo edit simcruisegame/osds --add-topic osds`.

## 3. The PC

All lanes under `D:\sc` are worktrees of `D:\simcruise`, so one command updates them all:

```powershell
git -C D:\simcruise remote set-url origin https://github.com/simcruisegame/osds.git
git -C D:\simcruise fetch origin
```

(Without it, the old URL still works through GitHub's redirect.)

## 4. The links

Merge `brand/osds-links` (README badges and links, website canonical, Open Graph, JSON-LD, sitemap, install, security, credits, issue template, installer URLs, release checklist). Old release notes and reports keep the old links; GitHub redirects them.

## 5. Check

- `https://github.com/simcruisegame/simcruise` opens `simcruisegame/osds`.
- `https://simcruisegame.github.io/simcruise/` goes to `https://simcruisegame.github.io/osds/`, and the new site shows the OSDS logo.
- The README badges show the release and the downloads.
- Aaron (off-site): submit `https://simcruisegame.github.io/osds/sitemap.xml` again in Google Search Console and Bing Webmaster Tools, and upload `docs/images/social-preview.png` again under Settings > General > Social preview.
