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

### Engine installed; first build and tests on UE 5.8.4

- UE **5.8.4** (changelist 58902052) finished installing at `C:\Program Files\Epic Games\UE_5.8`.
  The project's engine association is now `5.8`.
- Repository prepared for publishing as `dewilkinson/UE5Splats` (D17): the plugin was renamed
  from `Surfels` to `UE5Splats` (so paths in earlier entries now read `Plugins/UE5Splats/...`),
  `MyPlugin` was removed, an Apache-2.0 `LICENSE` and a root README were added, and the two local
  commits were rewritten to the GitHub noreply identity before any push. `BuildCodec.ps1` now
  also accepts a splatlab clone and copies its prebuilt codec.
- **First build:** failed with five errors, all in plugin code:
  - Three were UE 5.8 API changes: `CFLAG_ForceDXC` needs `ShaderCompilerCore.h`,
    `GetParameters()` returns a wrapper (`GetContents()`), and `FMatrix` transforms return
    `FVector4`.
  - One was a shadowed member name (`Bounds`, C4458).
  - Two deprecations were also fixed: `GetTranslatedWorldToClip`, `GetOnPostEngineInit()`.
  - The mesh-shader pipeline setup, `DispatchMeshShader` and the scene-proxy signature, flagged
    as the main risks, compiled as written.
- **Second build:** succeeded with no compiler warnings (editor target, Win64 Development).
- **Automation tests in the editor** (`Scripts/Test.ps1 -Filter Surfels`, headless, null RHI):
  5 of 5 passed in 1.7 s: `Surfels.Package.Synthetic` and `Surfels.Package.Samples` for cthulu,
  dwarves, strawberry and venus. All three plugin modules loaded, `SurfelsRendering` at
  `PostConfigInit`, with no plugin warnings in the log.
- **Not yet verified:** because the tests ran with the null RHI, Unreal hasn't compiled the
  plugin's shaders and nothing has been rendered. The importer and placing an actor haven't been
  exercised in the editor either.

### Published

- Created the public repository **https://github.com/dewilkinson/UE5Splats** (D17) and pushed
  `master`. GitHub reports it as public with the Apache License 2.0 detected; all three commits
  carry the noreply address, and none of the 72 published files is codec source, a codec binary
  or build output.

### Codec toolset mismatch found

- While building splatlab with VS 2022, its prebuilt `bluesec-codec.lib` failed to link
  (`unresolved external symbol __std_rotate`). The library's embedded compiler string shows it was
  built with **MSVC 14.51**; this machine has 14.44. A static library must be linked with the same
  MSVC version or a newer one, and UE 5.8's supported toolsets (14.44, 14.50) are both older.
- So UE5Splats' public route (stage splatlab's prebuilt library with `BuildCodec.ps1`) can't link
  on UE 5.8's toolsets yet. The builds and tests above used a codec built from source with 14.44.
  The README now states this as a known issue.

### First render check (step 1 seen working)

- Added `Scripts/RenderCheck.ps1` / `RenderCheck.py`: the editor runs off-screen on D3D12, imports
  `dwarves` (splat records) and `venus` (8-byte surfels), places each in a lit level with a cube
  in front of part of the model, and captures it through a SceneCapture2D on the mesh-shader path,
  the vertex-shader fallback, and with the model hidden. `Scripts/CompareCaptures.ps1` measures the
  differences. The plugin now logs which path draws (`LogSurfelsRendering`).
- **Hardware run** (AMD Radeon integrated graphics of the Ryzen 9 7950X3D, D3D12, SM6):
  - Both samples render. `dwarves` shows the gnomes upright, so the Y-up to Z-up conversion
    works, in their colours, at the coarsest level (25,830 surfels).
  - Depth occlusion works: the cube hides the part of the model behind it, and the cube region
    is unchanged against the baseline.
  - This GPU reports **Mesh Shader Tier 0** (feature level 12_1; confirmed by querying the D3D12
    driver directly), so both captures used the vertex-shader fallback.
- **WARP run** (`RenderCheck.ps1 -Warp`, Microsoft's software rasterizer, which reports Mesh Shader
  Tier 1): the mesh-shader path and the fallback both ran, and their captures are pixel-identical
  for both samples. They also match the hardware render closely (20.06% against 20.03% of pixels
  changed for `dwarves`). So the mesh path draws correctly. Its performance on real hardware is
  still untested. The WARP editor exited with an access violation after the log closed, during
  WARP teardown; no Unreal crash report was written.
- **Found: the scans render too dark.** `venus` is nearly black and `dwarves` dimmer than its
  stored colours, although the decoded colours are right (`venus` mean RGB 0.43 / 0.38 / 0.32,
  `dwarves` 0.58 / 0.55 / 0.41). The plugin writes stored colours into scene colour as linear,
  pre-exposed values, the equivalent of a faint emissive surface, and Unreal's exposure for a
  sunlit scene darkens them. 8-byte surfels also get surfels' fixed key light (x0.35 to 1).
  How baked splat colours should relate to Unreal's exposure is an open design question.

### Full-detail render and the emissive slider (2026-10-24)

- Added `EmissiveIntensity` to the Surfel component (D18) and options to the render check:
  `-Names`, `-Level`, `-Resolution`, `-Emissive "1,2,4"` and `-NoOccluder`.
- Rendered `dwarves` at level 0 (206,722 surfels) at 1920x1080 on the hardware GPU (fallback
  path), at emissive intensities 1, 2, 4, 8 and 16. At level 0 the discs form a near-continuous
  surface with fine detail (the stitching on the clothes, the painted dots on the skirt). Intensity
  2 looks closest to the stored colours.
- **Editor exit crash isolated:** after captures at 1920x1080, and on WARP at any size, the editor
  exits with 0xC0000005 *after* its log has closed. No Unreal crash report or Windows error event
  is written, and the captures are complete. It also happens with the surfel asset unassigned (the
  plugin draws nothing), so it isn't caused by the plugin. Hardware runs at 1280x720 exit cleanly.
  Not pursued further.
