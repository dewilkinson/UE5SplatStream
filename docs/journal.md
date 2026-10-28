# Splats UE plugin: journal

Dated notes on what was done, what was found and how it was verified. Newest last. Decisions are
in the decision record (D1, D2, ...; kept in the private repository), the build order and status in [plan.md](plan.md).

## 2026-10-23

### Environment

- Installed Visual Studio 2022 Community 17.14.42 with the C++ game development, desktop C++
  and .NET desktop workloads, the Unreal IDE and debugger integration, MSVC 14.44, Clang, and
  Windows SDKs 10.0.22621 and 10.0.26100. A reboot completed the install.
- Created the `PluginSandbox` host project with a starter plugin (`MyPlugin`) and an automation
  test, plus `Scripts/Build.ps1`, `Test.ps1` and `Package.ps1`, which find the newest installed
  UE5 through the Epic launcher's install list.
- Cloned `dewilkinson/splats` to `C:\github\splats`: all 14 LFS objects downloaded in full, and
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

### Findings in the splats source and data

- Chunk payloads are `ByteShuffle::DecompressShuffled` then `Unshuffle`; only those two codec
  calls are needed to decode a level.
- Positions are quantized against the global bounds: 8-byte point records with extents
  `max - min + 1e-5` at 10 bits per axis, splat records with `max(1e-6, max - min)` at 16 bits.
- Manifest chunks hold thousands to over a million splats per level; splats splits them into
  meshlets of 64 consecutive splats at load time, because its mesh shader draws at most 64 per
  group.
- Disc size comes from each level's median nearest-neighbour spacing times a coverage of 1.2
  (Auto Splat Size).
- Bundled samples: `venus` is v7 with 8-byte point records; `strawberry`, `dwarves` and `cthulu` are v8
  with 20-byte splat records.
- Possible normal-decoding mismatch in `Splat.hlsl` (D11).

### Step 1 written

- Wrote the plugin: four modules (D6), the asset, component, actor and scene proxy, the disc
  pass with mesh-shader and vertex paths, the view extension, and the importer.
- `Scripts/BuildCodec.ps1` builds the codec from the splats repo; the staged files are
  gitignored (D7).

### Verification (before the engine was installed)

- The format core (`SplatPackageCore`) and its tests compiled natively with MSVC `/W4 /WX`
  against the real codec library. Results:
  - `Splats.Package.Synthetic`: pass. It covers point8 and splat20 packages built with the
    codec's own encoder, level selection, meshlet splitting, record round-trips, Oct16 axes, and
    rejection of bad magic, truncation, mismatched payload sizes and a future version.
  - `Splats.Package.Samples`: pass for all four bundled scans, at both the coarsest level and
    level 0, with every decoded position inside the header bounds.

    | Scan | Coarsest level: splats / meshlets | Level 0: splats / meshlets |
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
  against, so the Unreal API calls listed in `Plugins/Splats/README.md` are the first things to
  check when it compiles.

### Engine installed; first build and tests on UE 5.8.4

- UE **5.8.4** (changelist 58902052) finished installing at `C:\Program Files\Epic Games\UE_5.8`.
  The project's engine association is now `5.8`.
- Repository prepared for publishing as `dewilkinson/UE5Splats` (D17): the plugin was renamed
  from `Splats` to `UE5Splats` (so paths in earlier entries now read `Plugins/UE5Splats/...`),
  `MyPlugin` was removed, an Apache-2.0 `LICENSE` and a root README were added, and the two local
  commits were rewritten to the GitHub noreply identity before any push. `BuildCodec.ps1` now
  also accepts a splatpak clone and copies its prebuilt codec.
- **First build:** failed with five errors, all in plugin code:
  - Three were UE 5.8 API changes: `CFLAG_ForceDXC` needs `ShaderCompilerCore.h`,
    `GetParameters()` returns a wrapper (`GetContents()`), and `FMatrix` transforms return
    `FVector4`.
  - One was a shadowed member name (`Bounds`, C4458).
  - Two deprecations were also fixed: `GetTranslatedWorldToClip`, `GetOnPostEngineInit()`.
  - The mesh-shader pipeline setup, `DispatchMeshShader` and the scene-proxy signature, flagged
    as the main risks, compiled as written.
- **Second build:** succeeded with no compiler warnings (editor target, Win64 Development).
- **Automation tests in the editor** (`Scripts/Test.ps1 -Filter Splats`, headless, null RHI):
  5 of 5 passed in 1.7 s: `Splats.Package.Synthetic` and `Splats.Package.Samples` for cthulu,
  dwarves, strawberry and venus. All three plugin modules loaded, `SplatPakRendering` at
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

- While building splatpak with VS 2022, its prebuilt `bluesec-codec.lib` failed to link
  (`unresolved external symbol __std_rotate`). The library's embedded compiler string shows it was
  built with **MSVC 14.51**; this machine has 14.44. A static library must be linked with the same
  MSVC version or a newer one, and UE 5.8's supported toolsets (14.44, 14.50) are both older.
- So UE5Splats' public route (stage splatpak's prebuilt library with `BuildCodec.ps1`) can't link
  on UE 5.8's toolsets yet. The builds and tests above used a codec built from source with 14.44.
  The README now states this as a known issue.

### First render check (step 1 seen working)

- Added `Scripts/RenderCheck.ps1` / `RenderCheck.py`: the editor runs off-screen on D3D12, imports
  `dwarves` (splat records) and `venus` (8-byte point records), places each in a lit level with a cube
  in front of part of the model, and captures it through a SceneCapture2D on the mesh-shader path,
  the vertex-shader fallback, and with the model hidden. `Scripts/CompareCaptures.ps1` measures the
  differences. The plugin now logs which path draws (`LogSplatPakRendering`).
- **Hardware run** (AMD Radeon integrated graphics of the Ryzen 9 7950X3D, D3D12, SM6):
  - Both samples render. `dwarves` shows the gnomes upright, so the Y-up to Z-up conversion
    works, in their colours, at the coarsest level (25,830 splats).
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
  sunlit scene darkens them. 8-byte point records also get splats' fixed key light (x0.35 to 1).
  How baked splat colours should relate to Unreal's exposure is an open design question.

### Full-detail render and the emissive slider (2026-10-24)

- Added `EmissiveIntensity` to the Splat component (D18) and options to the render check:
  `-Names`, `-Level`, `-Resolution`, `-Emissive "1,2,4"` and `-NoOccluder`.
- Rendered `dwarves` at level 0 (206,722 splats) at 1920x1080 on the hardware GPU (fallback
  path), at emissive intensities 1, 2, 4, 8 and 16. At level 0 the discs form a near-continuous
  surface with fine detail (the stitching on the clothes, the painted dots on the skirt). Intensity
  2 looks closest to the stored colours.
- **Editor exit crash isolated:** after captures at 1920x1080, and on WARP at any size, the editor
  exits with 0xC0000005 *after* its log has closed. No Unreal crash report or Windows error event
  is written, and the captures are complete. It also happens with the splat asset unassigned (the
  plugin draws nothing), so it isn't caused by the plugin. Hardware runs at 1280x720 exit cleanly.
  Not pursued further.

### Nanite living-room demo scene (2026-10-24)

- Added a test scene that mixes Nanite meshes with a splat scan: `/Game/Demo/Map_LivingRoom`.
  - `Scripts/FetchRoomAssets.ps1` downloads nine CC0 Poly Haven models (sofa, lounge chair,
    coffee table, side table, oil lamp, cabinet, bookshelf, brass vase, potted plant) as glTF with
    1K textures into `ThirdPartyAssets/` (gitignored, 15 MB).
  - `Scripts/BuildLivingRoom.ps1` imports each through Interchange with a copy of the glTF asset
    pipeline set to "Combine All" (multi-part models become one mesh), with Nanite on. It builds a
    6 x 6 m room (floor, ceiling, walls, a window in the back wall, the front open), a sun, sky
    atmosphere, a sky light, a lamp light and an auto-exposure post-process volume. It stands
    `dwarves` (level 0, scaled to 35 cm, emissive 1) on the coffee table, saves the level, and
    writes previews to `Saved/RenderCheck/living_room*.png` once shaders and lighting settle.
  - `Scripts/OpenDemo.ps1 -Map /Game/Demo/Map_LivingRoom` opens it at its `DemoCamera`.
- The splat renders correctly among the Nanite meshes: depth-tested against the table, at its
  stored colours with emissive 1 under the room's auto exposure. It casts no shadow on the table
  (splat shadow casting is still deferred, D4).
- Found while building it: Unreal's Python `unreal.Rotator(a, b, c)` takes **(roll, pitch, yaw)**.
  Positional `Rotator(0, yaw, 0)` had been tilting the furniture and pointing the suns in the earlier
  render-check scenes the wrong way. All scripts now use keyword arguments.
- Also: `-ExecutePythonScript` quits the editor when the script returns, so scripts that must keep
  the editor open (or wait for frames) run as `-ExecCmds="py <script>"` instead.

### Realistic lighting for the living room (2026-10-24)

- **The project had no global illumination.** The hand-written `Config/DefaultEngine.ini` never enabled
  what Unreal's project templates do; it now sets Lumen GI and reflections, mesh distance fields,
  virtual shadow maps and no static lighting.
- **Room:** closed, lit through a window in the back wall; herringbone parquet (satin, roughness x2),
  painted plaster and brown brick from Poly Haven (CC0); a lawn and light height fog outside.
  Sun, sky atmosphere, a real-time sky light, an auto-exposure volume held to a narrow range
  (0.8 to 1.2) so the interior keeps its contrast, a vignette.
- **Lights:** a built brick fireplace (surround, mantel, hearth, emissive embers, a warm 25 cd light),
  a Nanite chandelier (Poly Haven) with a 35 cd light, and the oil lamp.
- **Splats:** `dwarves` (level 0, 35 cm) on the coffee table; `cthulu` (level 1, 1.66 M splats,
  40 cm) on the mantel.
- Seen in the editor viewport: Lumen bounce light, reflections and virtual shadow maps all work, and
  both scans sit correctly in the depth buffer among the Nanite meshes. They don't respond to the
  room's lights (emissive, D18): step 1b. They cast no shadows: step 1a (D19, D20).

Problems found and fixed on the way (all in the demo scripts, not the plugin):

- **The level file was never replaced after the first build.** Deleting it failed while it was still
  referenced, `new_level` then refused the path, and the script built in an unsaved blank map while
  logging "saved". The editor therefore kept showing the first, broken build. The script now opens
  and clears the level, verifies it's editing `Map_LivingRoom`, and fails if the save fails.
- **`unreal.Color(r, g, b, a)` positional arguments are (B, G, R, A)**, like `Rotator`'s
  (roll, pitch, yaw): warm lights came out blue. All scripts use keyword arguments now.
- **Multi-part glTF models:** "Combine All" kept only the door of `modern_wooden_cabinet` (a mesh
  reused across nodes). It's replaced by `drawer_cabinet`, and the script now places every part an
  import yields instead of the first.
- **Translucent materials don't work with Nanite:** the sofa (fringe) and oil lamp (glass, flame) are
  ordinary static meshes; the other eight models are Nanite. Unreal still logs the warning when the
  level loads; not yet explained.
- **SceneCapture previews don't show Lumen**, so the scripted previews understate the lighting. The
  editor viewport is the reference. Unreal also logs "Abandoning remaining async distance field
  tasks" at every shutdown, whether or not any were left; it was a false lead.

### Demo content moved to UE5SplatsDemo (2026-10-24)

- The user added three Fab items (Content Examples, Cigar Room Environment, Coffee Shop Environment)
  to their library, intending them for PluginSandbox. None had reached the sandbox yet: Content
  Examples is being created as its own project, and the two environment packs still need
  "Add to Project".
- Created the private `UE5SplatsDemo` project (D21) and moved `Content/Demo` (the living room and its
  assets, 234 MB) into it. Its first build loaded the plugin straight from the sandbox's `Plugins`
  folder, rebuilt the living room there and rendered both scans.
- The fireplace was rebuilt as a settled fire: soot-black firebox, three charred logs, scattered
  embers in two strengths and a lower, dimmer fire light at the opening, instead of an evenly glowing
  firebox that read as a furnace.

### Lit mode, first step (2026-10-28)

- The user asked how to add basic lighting to a splat asset, and chose the G-buffer route: the opaque discs written into
  Unreal's GBuffer right after the base pass, so its own lights, shadows and Lumen shade them, rather than a separate
  splat framebuffer composited later.
- Done in `PostRenderBasePassDeferred_RenderThread`: scene colour plus GBufferA-C through the engine's `EncodeGBuffer`
  (default lit; precomputed shadow and velocity skipped), with its own depth-write binding, because after a full depth
  prepass the base pass binds depth read-only. Slots 1-3 are GBufferA-C in every non-Substrate layout; under Substrate
  or forward shading the scan stays unlit.
- Normals: a point record's stored oct16 normal, a splat's shortest axis, turned to face the camera.
- The stored colour is baked light, so **Lit Blend** splits it between emissive and albedo; specular is scaled by it
  too, so Lit Blend 0 matches the unlit discs exactly (checked by capture).
- Lit mode draws discs only, without the gap fill. Next steps: smoother normals (from the shadow mesh), lit Gaussians
  through a deferred pass of their own, and a de-lit albedo from the baker.
- Test case: the demo's `Map_LitDemo` (BP_Cthulhu beside BP_Cthulhu_Lit; `Scripts\BuildLitDemo.ps1 -Check`).
- Normal smoothing (same day): the splats' normals made the lit scan grainy, so the lit pass first rasterises the shadow
  mesh's smooth vertex normals (area-weighted, computed once on the CPU) and clip w into a screen-sized target, and each
  lit disc within 3% of the model's size of that surface leans its normal toward the mesh's by **Normal Smoothing**
  (default 0.7). The demo check gained a capture with smoothing off. A transient depth target created without
  `TexCreate_ShaderResource` tripped a D3D12 barrier assertion (deny-SRV resource transitioned to SRV); it has the flag now.

### SoundStage concept (2026-10-28)

- The user asked to package the scene's boundary, collision and stairway volumes as a SoundStage scene asset in the
  plugin, with a simple scene spec and accessors. Built as a first concept (`docs/soundstage.md`): `USoundStageAsset`,
  `ASoundStageActor`, a SoundStage factory (new, and import of `*.soundstage.json`), and the demo's pedestal moved into
  the plugin as `ASplatPakPedestal`.
- Splat references: first content-derived UUIDs were tried (a SHA-1 of the package), then dropped at the user's request
  for paths relative to a root directory, which keep the spec hand-editable; UUIDs may still be used internally later.
- The spec is in the package frame (metres, Y up) so SplatPak can read the same file.
- The palace demo is now one stage actor built from `Scenes/BishopsPalace.soundstage.json` (8 volumes, 5 pieces).
- Offline scene captures don't draw the shape components' wireframes (even with the Collision show flag), and new
  materials can still be compiling when they run (default checker on the plinths); the volumes are checked in the
  editor viewport instead.

### Rendering tab, pass isolation, loupe; palace stairs (2026-10-28)

- The palace's stair volumes were a guess; measured from the scan (package frame = PLY rotated 180 degrees about X) the
  centre flight runs 11.1 m at 15.4 degrees between balustrades at x = +/-1.5 m. Ray casts on the new volumes match the
  scan within 1 cm.
- User asked for Slate UI for rendering toggles and pass visualizers, and an inset like the app's loupe. Built as the
  SplatPak Rendering tab (Tools menu), `r.SplatPak.Pass.*` console variables, and `SSplatPakLoupe` (a scene capture along
  the cursor's ray into a 320x220 inset). The app's splat-outline / random-colour / centre loupe views need shader modes
  and are not ported yet.
- Found: in the editor viewport the splats never start streaming (no "preparing to stream" in any editor session, though
  the components tick); in play in editor the palace streams in 1.4 s. Still to fix.
