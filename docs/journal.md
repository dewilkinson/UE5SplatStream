# Surfels UE plugin: journal

Dated notes on what was done, what was found and how it was verified. Newest last. Decisions are
in [decisions.md](decisions.md), the build order and status in [plan.md](plan.md).

## 2026-10-23

### Environment

- Installed Visual Studio 2022 Community 17.14.42 with the C++ game development, desktop C++
  and .NET desktop workloads, the Unreal IDE and debugger integration, MSVC 14.44, Clang, and
  Windows SDKs 10.0.22621 and 10.0.26100. A reboot completed the install.
- Created the `PluginSandbox` host project with a starter plugin (`MyPlugin`) and an automation
  test, plus `Scripts/Build.ps1`, `Test.ps1` and `Package.ps1`, which find the newest installed
  UE5 through the Epic launcher's install list.
- Cloned `dewilkinson/surfels` to `C:\github\surfels`: all 14 LFS objects downloaded in full, and
  the `libs/cauldron` submodule checked out from the user's fork at its pinned commit.
- Unreal Engine itself needs the user's Epic sign-in; the launcher was opened on the Unreal
  Engine page.

### Design

- Compared Nanite with a dedicated splat renderer and chose the dedicated renderer inside Unreal
  (D1).
- Agreed colour space, TSR depth, lighting and mesh-shader decisions (D2 to D5).
- Agreed that `.sflw` structures stay authoritative inside a model, with Unreal's HZB used for
  meshlet occlusion, one splat sort across all models, and World Partition for very large scans
  (D12 to D15).

### Findings in the surfels source and data

- Chunk payloads are `ByteShuffle::DecompressShuffled` then `Unshuffle`; only those two codec
  calls are needed to decode a level.
- Positions are quantized against the global bounds: 8-byte surfels with extents
  `max - min + 1e-5` at 10 bits per axis, splat records with `max(1e-6, max - min)` at 16 bits.
- Manifest chunks hold thousands to over a million surfels per level; surfels splits them into
  meshlets of 64 consecutive surfels at load time, because its mesh shader draws at most 64 per
  group.
- Disc size comes from each level's median nearest-neighbour spacing times a coverage of 1.2
  (Auto Splat Size).
- Bundled samples: `venus` is v7 with 8-byte surfels; `strawberry`, `dwarves` and `cthulu` are v8
  with 20-byte splat records.
- Possible normal-decoding mismatch in `Surfels.hlsl` (D11).

### Step 1 written

- Wrote the plugin: four modules (D6), the asset, component, actor and scene proxy, the disc
  pass with mesh-shader and vertex paths, the view extension, and the importer.
- `Scripts/BuildCodec.ps1` builds the codec from the surfels repo; the staged files are
  gitignored (D7).

### Verification (before the engine was installed)

- The format core (`SurfelPackageCore`) and its tests compiled natively with MSVC `/W4 /WX`
  against the real codec library. Results:
  - `Surfels.Package.Synthetic`: pass. It covers surfel8 and splat20 packages built with the
    codec's own encoder, level selection, meshlet splitting, record round-trips, Oct16 axes, and
    rejection of bad magic, truncation, mismatched payload sizes and a future version.
  - `Surfels.Package.Samples`: pass for all four bundled scans, at both the coarsest level and
    level 0, with every decoded position inside the header bounds.

    | Scan | Coarsest level: surfels / meshlets | Level 0: surfels / meshlets |
    |---|---|---|
    | venus | 138,309 / 2,170 | 4,426,059 / 69,164 |
    | strawberry | 93,679 / 1,485 | 1,499,098 / 23,446 |
    | dwarves | 25,830 / 418 | 206,722 / 3,245 |
    | cthulu | 103,798 / 1,640 | 3,322,060 / 51,923 |

- All four shader entry points (`MainAS` as_6_5, `MainMS` ms_6_5, `MainVS` vs_6_0, `MainPS`
  ps_6_0) compiled and validated with DXC from Windows SDK 10.0.26100, with Unreal's
  `Common.ush` stubbed out.
- **Not yet verified:** the Unreal-facing C++ has not been compiled, and nothing has been seen
  rendering.

### Engine install

- The user started installing **Unreal Engine 5.8** from the Epic Games Launcher, into
  `C:\Program Files\Epic Games\UE_5.8`. 5.8 is newer than the engine versions step 1 was written
  against, so the Unreal API calls listed in `Plugins/Surfels/README.md` are the first things to
  check when it compiles.
