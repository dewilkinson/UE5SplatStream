# Surfels UE plugin: decision log

Every design decision for porting surfels to Unreal, newest last. Entries aren't rewritten
once made: a reversal or refinement is a new entry that names the one it supersedes, so this log
doubles as the history for reports.

**Status values:** *Agreed* (the user decided or approved it), *Proposed* (recommended, awaiting
the user), *Implementation* (made by Claude while writing code, not yet reviewed by the user),
*Superseded* (replaced by a later entry).

Related: [plan.md](plan.md) (build order and status), [journal.md](journal.md) (what was done and
how it was verified).

---

## D1. Integration architecture: view extension + RDG passes; scene proxy owns GPU state

- **Date:** 2026-10-23. **Status:** Agreed (the user chose this route over Nanite, then asked to build it).
- **Decision:** Surfels draws through its own render-graph (RDG) passes, inserted into Unreal's
  frame by a scene view extension. A `USurfelComponent` places a model; its scene proxy owns the
  decoded data and GPU buffers. The codec links as a prebuilt static library.
- **Why:** Nanite only rasterizes triangles. Meshing a scan loses view-dependent colour and soft
  edges, and drawing splats as tiny quads hits Nanite's known weakness with scattered
  geometry. Unreal's standard mesh-drawing path doesn't suit GPU-driven indirect draws or sorted
  blending.
- **Consequences:** surfels' shaders and data layouts port almost unchanged; Cauldron doesn't
  port. Surfels can coexist with Nanite meshes and occlude them correctly.

## D2. Colour space: keep `splatBlendSpace`

- **Date:** 2026-10-23. **Status:** Agreed.
- **Decision:**
  - Mode `1` (linear): splats drawn in `PrePostProcessPass_RenderThread`, converted sRGB to
    linear and multiplied by the view's pre-exposure, before TSR.
  - Mode `0` (display): drawn after the tonemapper (`SubscribeToPostProcessingPass`), matching
    the reference viewer.
- **Why:** linear blends correctly in the world; display space reproduces the reference look.
- **Consequences:** display mode runs after TSR, so it gets no TSR and its approximate depth can't
  help TSR; `TemporalFilterCS` is ported for that mode only. Post-tonemap targets are at output
  resolution and scene depth at render resolution, so depth-test UVs need rescaling.

## D3. TSR: splats write approximate depth

- **Date:** 2026-10-23. **Status:** Agreed.
- **Decision:** splats write depth where their alpha exceeds a threshold (the nearest "solid"
  splat), in the pre-post-process pass: after lighting, before TSR.
- **Why:** TSR reprojects from depth; without it, splats smear when the camera moves.
- **Consequences:** never write it earlier, or Lumen and SSAO would read it as a surface with no
  GBuffer. Static models reproject from depth alone; moving components will also need velocity.

## D4. Lighting

- **Date:** 2026-10-23. **Status:** Agreed.
- **Decision:** Gaussian splats are unlit (their lighting is baked in). Opaque discs are written
  into the GBuffer so Lumen lights them and they receive shadows.
- **Deferred on purpose:** shadow *casting* needs an extra depth pass per light. The user asked
  to be reminded about it later (tracked in [plan.md](plan.md) under Reminders).
- **See also:** D10 (step 1 draws discs after lighting as an interim).

## D5. Mesh shaders wherever possible

- **Date:** 2026-10-23. **Status:** Agreed.
- **Decision:** amplification + mesh shaders are the default wherever the platform and GPU
  support them; the instanced vertex-shader path is the fallback, not the baseline.
  - Mesh permutations compile only where `RHISupportsMeshShadersTier1` is true.
  - The path is chosen at runtime from `GRHISupportsMeshShadersTier1`; `r.Surfels.MeshShaders 0`
    forces the fallback.
  - The project targets DX12 / Shader Model 6 (`Config/DefaultEngine.ini`).
  - Both paths are built from the start so they stay in parity; the mesh path is verified first.

## D6. Module split: an early-loading rendering module without UObjects

- **Date:** 2026-10-23. **Status:** Implementation.
- **Decision:**
  - `SurfelsRendering` loads at `PostConfigInit` and holds the shaders, passes, view extension
    and format core.
  - `SurfelsRuntime` (Default) holds the asset, component, actor and proxy.
  - `SurfelsEditor` holds the importer.
  - `BluesecCodec` is an External module wrapping the static library.
- **Why:** global shader types must register before Unreal builds the global shader map, which
  forces `PostConfigInit`; UObject types are safer in a Default-phase module.
- **Risk:** `SurfelsRendering` links `Engine` for `SceneViewExtension.h`. If that loads Engine too
  early on the installed version, move the view extension into `SurfelsRuntime`.

## D7. The codec stays proprietary: built from source, staged locally, never committed

- **Date:** 2026-10-23. **Status:** Implementation.
- **Decision:** `Scripts/BuildCodec.ps1` builds `libs/bluesec-codec` from the surfels repo
  (Release, `/MD`, matching Unreal's CRT in every configuration) and stages headers and the `.lib`
  under `Source/ThirdParty/BluesecCodec/`, which is gitignored. `Build.ps1` runs it when needed.
- **Why:** the codec is "All Rights Reserved" and outside surfels' Apache-2.0 licence. The plugin
  repo must never carry its source or binaries.

## D8. Asset storage: the whole `.sflw` as bulk data in a `USurfelAsset`

- **Date:** 2026-10-23. **Status:** Implementation.
- **Decision:** the importer validates the package and stores it whole as non-inline bulk data;
  levels are decoded on demand and cached per asset, shared by every component showing them.
- **Why:** it cooks and packages like any Unreal asset, with no loose-file paths to manage, and
  bulk data supports async partial reads for streaming later (step 4).
- **Consequences:** step 1 decodes a whole level synchronously when the proxy is created.

## D9. Coordinate frame and units

- **Date:** 2026-10-23. **Status:** Implementation.
- **Decision:** packages stay in their own model space on the GPU. The component converts with a
  fixed basis: metres to centimetres (`UnitsToCentimeters = 100`), and Y-up right-handed to Z-up
  left-handed by swapping Y and Z (`bConvertYUpToZUp`). Both are per-component settings.
- **Why:** the shaders keep surfels' model-space maths unchanged; only the matrices differ.
  Matrices go through Unreal's translated world space, so large-world coordinates stay precise.

## D10. Step 1 draws opaque discs after lighting (interim; deviates from D4)

- **Date:** 2026-10-23. **Status:** Implementation, interim.
- **Decision:** step 1 draws hard-edged opaque discs in the pre-post-process pass: depth-tested,
  depth-writing, with surfels' own fixed key light, linear and pre-exposed.
- **Why:** the GBuffer layout depends on the engine version and the project's Substrate setting,
  which can't be checked until the engine is installed.
- **Consequences:** Lumen doesn't light the discs yet, and discs overwrite translucent surfaces in
  front of them. Both are fixed by moving the discs into the GBuffer (D4) on the installed engine.
- **Also:** discs are hard-edged and opaque rather than surfels' soft blended disc edge; soft edges
  return with the sorted splat pass (step 3).

## D11. Oct16 normals decoded as signed bytes

- **Date:** 2026-10-23. **Status:** Implementation; an open question for the surfels repo.
- **Decision:** the plugin decodes packed normals as the two signed bytes `PackNormalOct16` writes,
  matching surfels' CPU encoder and decoder.
- **Why:** `UnpackNormalOct16` in `Surfels.hlsl` reads them as unsigned (`b * 2/255 - 1`), which
  would turn +0.5 into -0.5. A neighbour-coherence check on venus slightly favoured signed
  decoding (0.72 vs 0.67) but isn't conclusive.
- **Follow-up:** confirm in the surfels repo; if its shader is wrong, surfels' own disc
  orientations and lighting are off.

## D12. `.sflw` structures stay authoritative inside a model; Unreal manages the model as a whole

- **Date:** 2026-10-23. **Status:** Agreed.
- **Decision:** inside a model, the `.sflw` hierarchy decides culling, level of detail, ordering
  and streaming. Unreal handles each model as one primitive: whole-model frustum, distance and
  visibility culling, the transform, scene membership and editor bounds.

  | `.sflw` structure | Owner |
  |---|---|
  | Morton chunks (bounds, centre, radius) | Surfels |
  | Per-chunk LOD pyramid with `geometricError` | Surfels (screen-space-error traversal) |
  | 64-surfel meshlets | Surfels (amplification-shader culling) |
  | Normal cones | Surfels |
  | Chunk order and per-splat GPU sort | Surfels |
  | Interior occlusion volume | Surfels, drawn into Unreal's depth buffer |
  | Bounding octahedron, detail grid | Surfels (codec streaming order) |

- **Why:** Unreal has no general mechanism for culling inside an object. Nanite's cluster culling
  only works on Nanite's own mesh format; Unreal's LOD picks one level per object; translucency
  sorting orders whole objects, not the splats inside them. One primitive per chunk would mean
  thousands of scene proxies, no LOD awareness and a broken single GPU-driven draw.
- **Consequence for the code:** step 1 flattens chunks into one meshlet list and ignores the
  manifest's chunk bounds and `geometricError`. Step 2 restores the two-level hierarchy
  (chunks hold their bounds, error and level pyramid, and point to their meshlets), which step 4's
  traversal needs.

## D13. Use Unreal's HZB for meshlet occlusion culling

- **Date:** 2026-10-23. **Status:** Agreed (step 2).
- **Decision:** the amplification shader tests meshlet bounds against Unreal's hierarchical
  Z-buffer, so meshlets hidden behind scene geometry are never drawn.
- **Why:** it's the most useful thing Unreal can add inside a model, and it's free: Unreal
  builds the HZB every frame.
- **Limits:** discs drawn after the HZB is built don't contribute to it, so meshes cull surfels but
  surfels don't cull meshes. Per-pixel occlusion is correct both ways regardless (depth test and
  depth write). Whether moving discs into the GBuffer pass changes this is to be confirmed on the
  installed engine.

## D14. Sort the splats of every visible model in one pass

- **Date:** 2026-10-23. **Status:** Agreed (step 3).
- **Decision:** the splat sort runs once per view across all visible splat drawables, not per
  model.
- **Why:** Unreal orders translucent objects per object, and per-model sorts composite wrongly
  wherever two splat models overlap on screen.

## D15. Very large scans: World Partition cells

- **Date:** 2026-10-23. **Status:** Agreed as a later item (after step 5).
- **Decision:** city-scale packages (e.g. `paris_lille_street`) can be split into World Partition
  cells, one surfel actor per cell. Unreal decides which regions are loaded; surfels streams the
  detail inside each.
- **Why:** one primitive per scan is too coarse at that scale, and World Partition is Unreal's
  standard way to stream large worlds.

## D16. Expose surfels' quality controls through Unreal's scalability settings

- **Date:** 2026-10-23. **Status:** Proposed (raised with D12; would be scheduled with step 4).
- **Decision:** surfels' error threshold and splat budget become `r.Surfels.*` console variables
  that Unreal's scalability groups and screen percentage can drive.
