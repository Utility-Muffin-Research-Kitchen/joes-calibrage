# Joe's Calibrage

Analog-stick calibration for the **Miniloong Pocket 1 (MLP1)**, built for the
[Leaf](https://github.com/Utility-Muffin-Research-Kitchen) launcher. It measures
the stick's real physical range, saves a per-device calibration profile, and lets
Leaf normalize the stick so games and apps see the full range.

This is a UMRK fork of [`Helaas/nextui-Joe-s-Calibrage-pak`](https://github.com/Helaas/nextui-Joe-s-Calibrage-pak),
retargeted from NextUI (Miyoo/TrimUI) to MLP1/Leaf. The UI runs on **Catastrophe**
(Leaf's toolkit), so it inherits the launcher's theme, font, and chrome.

## The problem it solves

The MLP1 stick under-throws: the kernel advertises a full signed 16-bit range
(`-32768..32767`), but the stick physically only reaches about `-22000..24000`,
and the exact range varies from unit to unit. Anything that trusts the advertised
range therefore sees a partial throw — for example, an analog game where the stick
only ever reaches a walk instead of a full run.

Joe's Calibrage captures each unit's real range so Leaf can remap it back to the
full advertised range.

## How it works

1. **Capture** — the app reads the raw stick from the kernel input device
   (evdev `ABS_X` / `ABS_Y`) while you roll the stick around its edge, then
   records the center while you let it rest.
2. **Profile** — it writes a calibration profile to the durable user-data tree:

   ```text
   $USERDATA_PATH/input/loong-gamepad-calibration.json
   ```

   On MLP1 that resolves to
   `/mnt/sdcard/.userdata/mlp1/input/loong-gamepad-calibration.json`.
3. **Normalize** — Leaf's input layer (`jawakad`'s MLP1 input proxy) reads the
   profile and remaps `ABS_X`/`ABS_Y` to the full range with deadzone-aware
   per-axis scaling, so every consumer of the virtual gamepad — RetroArch and
   the launcher alike — gets the corrected throw. The profile is loaded at
   launcher startup.

## Menu

The MLP1 has a single analog stick, so the UI is single-stick.

- **Test Stick** — live view of the stick position, to confirm what apps see.
- **Calibrate** — roll the stick around the edge (range), then rest it (center),
  then save.
- **View Values** — the current saved calibration and diagnostics.
- **Restore Backup** — restore a previous calibration backup.

Footer: **A** select / save, **B** back / cancel.

## Localization

The UI and engine messages are fully translated to Simplified Chinese, using
the same i18n shape as [Thing-File](../../Thing-File): English strings are the
keys (`T("Calibrate")` in the sources), `i18n/zh_CN.po` is the single source of
truth, and `make i18n-build` converts it to the TSV table the app loads
(`build/i18n/zh_CN.tsv`, staged into the pak's `res/i18n/` by
`make package-mlp1`).

At startup the language is resolved from `$UMRK_LANGUAGE`, falling back to
`$JAWAKA_LANGUAGE` (both exported by the Leaf launcher's `env.sh`); missing or
unknown means English. The first valid table wins, searched in this order:

1. `$USERDATA_PATH/joes-calibrage/i18n/<lang>.tsv` — live reviewer override
2. `$JOES_CALIBRAGE_I18N_DIR/<lang>.tsv` — native dev build dir
3. `<pak>/res/i18n/<lang>.tsv` — the packaged table (`launch.sh` runs with the
   pak directory as the working directory)

Maintenance: run `make i18n-pot` after any UI-string change (it rewrites
`i18n/joes-calibrage.pot` from the `T()` calls), keep `i18n/*.po` reviewed to
the `make i18n-check` coverage gate, then rebuild the TSVs. Chinese rendering
relies on the Leaf appearance-snapshot font carrying CJK glyphs (the same
shared Source Han face Thing-File uses), so no font ships in this pak.

## Building

Catastrophe and Jawaka resolve as workspace siblings (the canonical UMRK layout:
`joes-calibrage`, `Catastrophe`, `Jawaka` side by side). No submodules.

```bash
# Engine unit tests (toolkit-agnostic, runs on the host)
make test-native

# Cross-compile the aarch64 binary + assemble the .pak (Docker mlp1-toolchain)
make package-platform PLATFORM=mlp1
#   -> build/mlp1/package/Joe's Calibrage.pak

# Desktop dev build (needs the sibling Catastrophe checkout + brew SDL2)
make mac
```

Leaf stages the app through its own dispatcher
(`make stage-app APP=joes-calibrage DEVICE=mlp1`), which calls
`make package-platform PLATFORM=mlp1` and deploys the resulting pak.

## License

MIT, inherited from the upstream project. See [LICENSE](LICENSE). Upstream
attribution to Helaas is retained; the calibration engine (capture math, config
parsing) is the upstream's, adapted here for MLP1's signed evdev input.
