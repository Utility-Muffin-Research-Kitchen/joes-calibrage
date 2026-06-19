# Joe's Calibrage — agent notes

This repo is a UMRK fork of `Helaas/nextui-Joe-s-Calibrage-pak`, retargeted to
the **Miniloong Pocket 1 (MLP1)** and the Leaf launcher. The MLP1 analog stick
under-throws (kernel advertises `-32768..32767`, physical reach is only
~`-22k..24k`), so the app captures a per-device calibration profile and feeds it
to Leaf's input layer.

This is now a **Leaf-native, MLP1-only** app: the UI runs on **Catastrophe**
(Leaf's toolkit), not Apostrophe, so it inherits Jawaka's appearance snapshot
like other UMRK apps. The upstream NextUI/Apostrophe path and the
my355/tg5040/tg5050 device builds have been removed; the calibration *engine*
(`platform.c`, `config.c`, `calibration.c`, `raw_input.c`) is toolkit-agnostic
and keeps those platform entries for the unit tests. Keep the MIT license and
upstream attribution intact.

## Cross-repo plan

The full adaptation design + phased implementation lives in the sibling
workspace, not here:

- `../umrk-workspace/plans/joes-calibrage-mlp1.md`

Read it before MLP1 work. Key points:

- **Upstream is serial/UART** (`src/raw_input.c`, `ttyS*`, per-device packet
  decoders). MLP1 has none of that — it reads evdev `EV_ABS` (`ABS_X`/`ABS_Y`)
  from the `Loong Gamepad`. The MLP1 port replaces the raw reader.
- The `jc_platform_info` table (`src/platform.c`) and the capture/normalize math
  (`src/calibration.c`) are reusable; MLP1 needs a **signed** raw range
  (`raw_min=-32768`, `raw_max=32767`).
- MLP1 has **one** analog stick — hide/disable the right-stick flow.
- MLP1 writes a JSON calibration profile under `$USERDATA_PATH/input/`, not the
  stock `joypad.config` files.
- The Leaf runtime consumes the profile in
  `Jawaka/internal/platform/input_proxy_mlp1.c` (already landed as the Phase 0
  runtime fix).

## Build

- Native engine tests: `make test-native` (toolkit-agnostic; no Catastrophe).
- Device build + pak: `make package-platform PLATFORM=mlp1` — cross-compiles via
  the `mlp1-toolchain` container (`ports/mlp1/Makefile`, run by
  `scripts/build-mlp1.sh`) and assembles `build/mlp1/package/Joe's Calibrage.pak`.
- Catastrophe and Jawaka (for `third_party/cjson`) resolve as workspace
  **siblings** — no submodules. Desktop dev: `make mac` (needs sibling
  Catastrophe + brew SDL2).
