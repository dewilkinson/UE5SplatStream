# UE5Splats

Renders `.sflw` packages (surfel and Gaussian splat scans) inside Unreal's renderer.
A port of the DX12 renderer in [splatlab](https://github.com/dewilkinson/splatlab), following the
design record in [docs/](../../docs/README.md): decisions, plan and journal.

## Status: step 1 (opaque discs, one resident level)

- Import a `.sflw` by dragging it into the Content Browser (v4 to v8 packages, both record formats).
- Place a **Surfel Actor**, or add a **Surfel Component** to any actor, and assign the asset.
- One level stays resident (`Resident Level`, -1 = coarsest). It's decoded once and shared by
  every component that shows it.
- Discs are drawn opaque after lighting and before TSR. They are depth-tested against the scene
  and write depth, so Nanite meshes and surfels occlude each other and TSR reprojects them.
- **Mesh shaders are the default** (`r.Surfels.MeshShaders 1`): an amplification shader frustum-
  culls 32 meshlets per group, and a mesh shader emits 64 discs per meshlet. Hardware without Mesh
  Shader Tier 1 (or `r.Surfels.MeshShaders 0`) uses the instanced vertex-shader fallback.
- The colour is surfels' own: its fixed two-sided key light on 8-byte surfels and unlit DC colour
  on splat records, converted to linear and pre-exposed. Unreal's lights don't affect it yet.

## Layout

| Module | Loads | Contents |
|---|---|---|
| `SurfelsRendering` | PostConfigInit | Shaders, the disc pass, the view extension, the `.sflw` format core. No UObjects (global shaders must register before the shader map is built). |
| `SurfelsRuntime` | Default | `USurfelAsset`, `USurfelComponent`, `ASurfelActor`, the scene proxy. |
| `SurfelsEditor` | Editor | `.sflw` import factory. |
| `BluesecCodec` | External | The proprietary codec as a prebuilt static library. |

`Source/SurfelsRendering/Public/SurfelPackageCore.h` is plain C++17, so the format code and its
tests also build and run outside Unreal (see Testing).

## Building

1. `Scripts/BuildCodec.ps1 -CodecRepo <checkout>` stages bluesec-codec into
   `Source/ThirdParty/BluesecCodec/`: from a splatlab clone it copies the prebuilt library, from
   the private surfels repo it builds the library from source. The staged files aren't covered by
   this repository's licence and are **gitignored**; never commit them. `Scripts/Build.ps1` runs it
   automatically when the library is missing.
2. `Scripts/Build.ps1` builds the editor target. UE 5.8 or newer is required (the code uses
   5.8 APIs such as `GetTranslatedWorldToClip` and `GetOnPostEngineInit`).
3. The project targets DX12 with Shader Model 6 (`Config/DefaultEngine.ini`), which the mesh
   shader path needs.

## Testing

- `Scripts/Test.ps1 -Filter Surfels` runs, headless:
  - `Surfels.Package.Synthetic` builds small surfel8 and splat20 packages in memory with the
    codec's own encoder, then checks level selection, meshlet splitting, record round-trips, Oct16
    decoding, and that corrupt files (bad magic, truncation, mismatched sizes, a future version)
    are rejected.
  - `Surfels.Package.Samples.<name>` decodes the coarsest and finest levels of every package
    under `C:/github/surfels/assets` (set `SURFELS_REPO` to a surfels or splatlab checkout) and
    checks every position against the header bounds.
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

Still to confirm at runtime: that `SurfelsRendering` loading at `PostConfigInit` while linking
`Engine` is fine. If it isn't, move the view extension into `SurfelsRuntime`.

## Known gaps (by plan)

- **Lighting:** step 1 writes colour after Unreal's lighting, so Lumen doesn't light the discs
  yet. Writing them into the GBuffer before lighting (agreed decision 3) depends on the project's
  GBuffer layout (Substrate or legacy), so it's a separate step, done against the installed engine.
- **Translucency:** discs drawn after the translucency pass overwrite translucent surfaces in
  front of them. Moving the opaque discs into the GBuffer pass fixes this as well.
- **No splat shadow casting** (deferred on purpose; needs a depth pass per light).
- **No streaming yet:** a whole level is decoded synchronously when the proxy is created (step 4).
- **No normal-cone culling, dithered cross-fades, spherical harmonics or sorted Gaussians yet**
  (steps 2 to 4).
- **Editor picking:** clicking the discs doesn't select the actor; select it in the Outliner.

## Open question about surfels itself

`PackNormalOct16` (WaveletTypes.h) writes the two octahedral components as **signed** bytes,
but `UnpackNormalOct16` in `Surfels.hlsl` decodes them as **unsigned** (`b * 2/255 - 1`), so +0.5
would come back as -0.5. The plugin decodes them signed, matching the CPU encoder and the CPU
decoder. A neighbour-coherence check on venus slightly favoured signed decoding (0.72 vs 0.67
at the coarsest level), but that isn't conclusive. Worth checking in the surfels repo, because
if the shader is wrong there, its disc orientations and lighting are off.
