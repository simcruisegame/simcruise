## What this changes

<!-- One stream of work per pull request. Say what changed and why. -->

## How it is measured

<!-- Which automation test (scripts\check.ps1) or lab/ study covers it, and the numbers before and after. -->

| Check | Before | After |
|---|---|---|
| | | |

## Checklist

- [ ] `scripts\check.ps1` passes (this includes `SimCruise.Vehicle.CarTrack.<car>`, which keeps every car inside its calibration bands in `docs/cars.md`).
- [ ] `sh scripts/check-helper.sh` passes if the helper changed.
- [ ] No keys, tokens, personal paths or personal information are added.
- [ ] New data or assets have their source and licence recorded in `licenses/` (and `Credits.txt` if bundled).
- [ ] `CHANGELOG.md` has an entry under *Unreleased* if players will notice the change.

## What is still unverified

<!-- For example: tested headless only, not on a Mac; frame rate not measured. -->
