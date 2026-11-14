# Security policy

## Supported versions

Security fixes go into the latest release and the `main` branch. Older releases are not patched; please update.

| Version | Supported |
|---|---|
| Latest release | Yes |
| `main` | Yes |
| Anything older | No |

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Report privately through GitHub: go to the repository's [Security tab](https://github.com/simcruisegame/simcruise/security) and choose **Report a vulnerability**, or open <https://github.com/simcruisegame/simcruise/security/advisories/new> directly. Only the maintainers can see the report.

Include what you found, how to reproduce it, which version or commit you tested, and what an attacker could do with it. We aim to acknowledge reports within a week and to agree a disclosure date with you once a fix is ready. We credit reporters in the advisory unless you ask us not to.

## What counts

OSDS downloads and parses data from public services and runs a helper executable, so these are especially in scope:

- Malformed map, image, lidar or weather data that crashes the helper or the game, writes outside the cache folder, or runs code.
- Anything that makes the game contact a host other than the data services listed in [docs/data-sources.md](docs/data-sources.md), or sends information about the player.
- Cache poisoning, path traversal in cache keys, or unbounded disk or memory use triggered by remote data.
- Problems in the build and release scripts.
- Secrets, keys or personal information committed to the repository.

Out of scope: inaccurate map data (open an ordinary issue, or fix it upstream in OpenStreetMap), and the macOS Gatekeeper warning for the unnotarized app, which is expected and explained in [docs/install.md](docs/install.md).

## How the project protects itself

- **No secrets in the repository.** The game needs no API keys or accounts; CONTRIBUTING.md forbids data sources that do. GitHub secret scanning with push protection is enabled, so known key and token formats are rejected when pushed.
- **No telemetry.** The game sends no analytics or crash reports. The only network traffic is the helper's requests to public data services.
- **Pinned dependencies.** JavaScript dependencies are locked in `bun.lock`, and Dependabot security alerts flag known vulnerabilities in them.
- **Reviewed changes.** `main` is protected: changes land through reviewed pull requests whose author has run the headless checks.
