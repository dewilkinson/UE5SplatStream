# SplatPak

Renders `.gstx` packages (compressed 3D Gaussian splat scans) inside Unreal's renderer.
A port of the DX12 renderer in [splatpak](https://github.com/dewilkinson/splatpak), following the
design record in [docs/](../../docs/README.md): decisions, plan and journal.

## Status: opaque discs, streaming

- Import a `.gstx` by dragging it into the Content Browser (format 14, the only version read; an older package, or
  an `.sflw`, is refused with a prompt to bake it again in SplatPak).
- Place a **SplatPak Actor**, or add a **SplatPak Component** to any actor, and assign the asset.
- **Streaming** (on by default, the component's **Streaming** category): the model streams the way SplatPak streams it.
  The codec library's streaming runtime decides, under a simulated bandwidth, which blocks of which levels are
  resident, follows the camera (the active level viewport in the editor, the player's camera in a game), and hands back
  the render list each frame: per-block levels, dithered cross-fades between levels, **Release Unused LOD Segments**,
  the **Conservative** or **Greedy** policy, and the **Refinement Visualizer**'s arrival glow in each level's colour.
  **Evict** (a Details button, and Blueprint-callable) drops everything but the two coarsest levels so the model streams
  in again; `Restream`, `GetLevelResidency`, `GetStreamProgress` and `GetStreamStats` drive a HUD. As in SplatPak the
  network is simulated: every level is decoded into memory when the model starts streaming, on worker threads, which
  takes a few seconds for a large scan (the resident level is drawn meanwhile).
- With streaming off, one level stays resident (`Resident Level`, -1 = coarsest). It's decoded once and shared by
  every component that shows it.
- The classes were renamed with the SplatPak prefix (`USplatPakAsset`, `USplatPakComponent`, `ASplatPakActor`);
  `Config/DefaultSplatPak.ini` redirects the old names, so existing assets and levels load.
- Discs are drawn opaque after lighting and before TSR. They are depth-tested against the scene
  and write depth, so Nanite meshes and splats occlude each other and TSR reprojects them.
- **Mesh shaders are the default** (`r.SplatPak.MeshShaders 1`): an amplification shader frustum-
  culls 32 meshlets per group, and a mesh shader emits 64 discs per meshlet. Hardware without Mesh
  Shader Tier 1 (or `r.SplatPak.MeshShaders 0`) uses the instanced vertex-shader fallback.
- The colour is SplatPak's own: its fixed two-sided key light on 8-byte point records and unlit DC colour
  on splat records, converted to linear and pre-exposed. Unreal's lights don't affect it, unless lit mode is on.
- **Lit mode** (the component's **Lighting** settings, `Lit`): the discs are written into Unreal's GBuffer right after
  the base pass (`PostRenderBasePassDeferred_RenderThread`: scene colour plus GBufferA-C, depth-writing), so the scene's
  lights, their shadows (virtual shadow maps included) and Lumen shade them. Each disc's normal is a point record's
  stored normal or a splat's shortest axis, turned to face the camera. Those are noisy, so where a disc lies on the
  shadow mesh's surface (within 3% of the model's size) its normal leans toward the mesh's smooth vertex normal by
  **Normal Smoothing** (default 0.7; a screen-sized normal pass of the shadow mesh before the discs). The stored colour is baked light, so **Lit
  Blend** splits it: 0 keeps it all emissive (the unlit look), 1 makes it all albedo; **Albedo Scale**, **Lit
  Roughness** and **Lit Specular** set the rest of the surface. Lit mode always draws discs (no Gaussians) and no gap
  fill, and needs the deferred renderer without Substrate (otherwise the scan draws unlit). It is a first step: the
  albedo is the baked colour, not a de-lit one. The demo's `Map_LitDemo` (built by
  its `Scripts\BuildLitDemo.ps1`, `-Check` for captures) shows the unlit and the lit Cthulhu side by side.
- **Shadows, Lumen and collision from the shadow mesh** (format v12): importing a package that carries one also creates
  `<name>_ShadowMesh`, a static mesh with three LODs following the model's visible surface (distance field on,
  complex-as-simple collision, no Nanite). Each Splat Component places it hidden: it casts the model's shadow from
  every light type (virtual shadow maps included), feeds Lumen through its distance field and blocks traces. The
  component's **Shadow Mesh** settings switch each of the three off. A package without a mesh imports as before, casts
  no shadow, and the import log suggests a rebake.

## Layout

| Module | Loads | Contents |
|---|---|---|
| `SplatPakRendering` | PostConfigInit | Shaders, the disc pass, the view extension, the `.gstx` format core. No UObjects (global shaders must register before the shader map is built). |
| `SplatPak` | Default | `USplatPakAsset`, `USplatPakComponent`, `ASplatPakActor`, the scene proxy. |
| `SplatPakEditor` | Editor | `.gstx` import factory. |
| `GSTx` | External | The proprietary codec as a prebuilt static library. |

`Source/SplatPakRendering/Public/SplatPakPackageCore.h` is plain C++17, so the format code and its
tests also build and run outside Unreal (see Testing).

## Building

1. `Scripts/BuildCodec.ps1 -CodecRepo <checkout>` stages the GSTx codec library into
   `Source/ThirdParty/GSTx/`: from a splatpak clone it copies the prebuilt library, from
   the private 3DGSStream repo it builds the library from source. The staged files aren't covered by
   this repository's licence and are **gitignored**; never commit them. `Scripts/Build.ps1` runs it
   automatically when the library is missing.
2. `Scripts/Build.ps1` builds the editor target. UE 5.8 or newer is required (the code uses
   5.8 APIs such as `GetTranslatedWorldToClip` and `GetOnPostEngineInit`).
3. The project targets DX12 with Shader Model 6 (`Config/DefaultEngine.ini`), which the mesh
   shader path needs.

## Testing

- `Scripts/Test.ps1 -Filter SplatPak` runs, headless:
  - `SplatPak.Package.Synthetic` builds small point8 and splat20 packages in memory with the
    codec's own encoder, then checks level selection, meshlet splitting, record round-trips, Oct16
    decoding, and that corrupt files (bad magic, truncation, mismatched sizes, a future version)
    are rejected.
  - `SplatPak.Package.Samples.<name>` decodes the coarsest and finest levels of every package
    under `C:/github/3DGSStream/assets` (set `SPLATPAK_REPO` to a 3DGSStream or splatpak checkout) and
    checks every position against the header bounds.
- `Scripts/RenderCheck.ps1` renders `dwarves` and `venus` in the editor on the GPU (off-screen) on
  both paths and with the model hidden; `Scripts/CompareCaptures.ps1` compares the captures.
  `-Warp` uses Microsoft's software rasterizer, which supports mesh shaders, for GPUs that don't.
  The log line `LogSplatPakRendering: Drawing splats with ...` says which path drew.
- Before the engine was installed, both suites were run natively against the same source
  (MSVC `/W4 /WX`) and passed for all four bundled scans. The four shader entry points
  (`as_6_5`, `ms_6_5`, `vs_6_0`, `ps_6_0`) compile and validate with the Windows SDK's DXC.

## Engine compatibility

Builds without warnings against **UE 5.8.4** (editor target, Development). Bringing step 1 up on
5.8 needed five fixes; the mesh-shader setup, `DispatchMeshShader` and the scene-proxy signature
compiled as written:

- `CFLAG_ForceDXC` now needs `ShaderCompilerCore.h`.
- `FRDGUniformBuffer::GetParameters()` returns a wrapper; `GetContents()` gives the struct.
- `FMatrix::TransformVector` / `TransformPosition` return `FVector4`, so they need an explicit
  `FVector` before narrowing to `FVector3f`.
- `GetTranslatedViewProjectionMatrix` became `GetTranslatedWorldToClip`, and
  `FCoreDelegates::OnPostEngineInit` became `GetOnPostEngineInit()` (both deprecated in 5.8).
- A local named `Bounds` shadowed a scene-proxy member (C4458 is an error in Unreal builds).

Still to confirm at runtime: that `SplatPakRendering` loading at `PostConfigInit` while linking
`Engine` is fine. If it isn't, move the view extension into `SplatPak`.

## Known gaps (by plan)

- **Lighting:** unlit discs are written after Unreal's lighting, so its lights don't reach them. Lit mode writes them
  into the GBuffer instead, for the legacy GBuffer layout only; Substrate projects, sorted Gaussians and the gap fill
  stay unlit. Lit Gaussians need a deferred pass of their own (blended normal and albedo, shaded once).
- **Translucency:** unlit discs drawn after the translucency pass overwrite translucent surfaces in
  front of them. Lit mode, drawn with the base pass, does not.
- **No per-splat shadow casting** (the shadow mesh stands in; true splat shadows need a depth pass per light). The splats
  do not receive shadows yet either; when they do, a depth bias will keep the shadow mesh, which lies on the visible
  surface, from shadowing its own splats.
- **Simulated network:** streaming decodes every level into memory first (the package size in records, about 400 MB
  for Cthulhu); real reads from disk come with the runtime's read phase.
- **No normal-cone culling, spherical harmonics or sorted Gaussians yet** (steps 2 to 4).
- **Editor picking:** clicking the discs doesn't select the actor; select it in the Outliner.

## Open question about SplatPak itself

`PackNormalOct16` (WaveletTypes.h) writes the two octahedral components as **signed** bytes,
but `UnpackNormalOct16` in `Splat.hlsl` decodes them as **unsigned** (`b * 2/255 - 1`), so +0.5
would come back as -0.5. The plugin decodes them signed, matching the CPU encoder and the CPU
decoder. A neighbour-coherence check on venus slightly favoured signed decoding (0.72 vs 0.67
at the coarsest level), but that isn't conclusive. Worth checking in the 3DGSStream repo, because
if the shader is wrong there, its disc orientations and lighting are off.
