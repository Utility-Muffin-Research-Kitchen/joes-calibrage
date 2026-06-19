# Joe's Calibrage — agent notes

This repo is a UMRK fork of `Helaas/nextui-Joe-s-Calibrage-pak`, adapted to add
a **Miniloong Pocket 1 (MLP1)** port for the Leaf launcher. The MLP1 analog
stick under-throws (kernel advertises `-32768..32767`, physical reach is only
~`-22k..24k`), so the adaptation captures a per-device calibration profile and
feeds it to Leaf's input layer.

Upstream stays the source of truth for the shared engine. Keep the MIT license
and upstream attribution intact.

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

- Native tests: `make test-native`
- Per-platform builds use Docker toolchains (`make my355 | tg5040 | tg5050`);
  the MLP1 port adds `ports/mlp1/` against the `mlp1-toolchain` image.
- First-time: `git submodule update --init` (Apostrophe UI toolkit).
