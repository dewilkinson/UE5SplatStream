# Splats UE plugin: plan and status

The build order and where each step stands. Decisions referenced as D*n* are in
[decisions.md](decisions.md); dated progress and verification are in [journal.md](journal.md).

**Status values:** *Not started*, *In progress*, *Written, not compiled* (code exists but hasn't
been built against Unreal), *Done* (built, tested, and seen working in the engine).

## Prerequisites

| Item | Status |
|---|---|
| Visual Studio 2022 17.14 with the C++ game workloads, MSVC 14.44, Windows SDKs 10.0.22621 / 26100 | Done |
| Plugin sandbox project (`PluginSandbox`) with build, test and package scripts | Done |
| splats repo cloned with LFS assets and the Cauldron submodule (`C:\github\splats`) | Done |
| bluesec-codec builds and stages into the plugin (D7) | Done |
| Unreal Engine 5.8.4 installed from the Epic Games Launcher | Done |
| First build of the plugin against the installed engine | Done: builds without warnings; 5/5 automation tests pass |

## Build order

### Step 1: one resident level as opaque discs

**Status:** Renders on UE 5.8.4: both record formats, depth occlusion against scene meshes, fallback path on hardware, mesh path verified on WARP.

- `.sflw` import into a `USplatAsset` (D8); `USplatComponent` / `ASplatActor`; scene proxy.
- One level resident (coarsest by default), split into 64-splat meshlets.
- Opaque discs after lighting, depth-tested and depth-writing (D10, interim).
- Mesh-shader path by default with the vertex fallback (D5); amplification-shader frustum culling.
- Both record formats: 8-byte point records and 20-byte splat records (drawn as discs, DC colour).
- Tests: synthetic package round-trips and corruption checks; all bundled scans decode in bounds.

**Remaining:** time the mesh path on a GPU with Mesh Shader Tier 1. Colours are emissive with a per-component
Emissive Intensity (D18).

### Step 1a: splat shadow casting

**Status:** Scheduled next (2026-10-24, D19). Approach chosen: shadow proxy from the baked voxel shell (D20).

- Option 1: draw the splats in Unreal's own shadow passes through a custom vertex factory and a
  shadow-only mesh batch. Exact per-disc shadows from every light type, virtual shadow maps
  included; the most engine-version-sensitive code in the plugin.
- Option 2: a hidden shadow-proxy mesh (voxelised points, or the package's baked occlusion
  volume) that casts shadows only. No new rendering code, can double as collision; shadows are
  approximate at the voxel size.
- Test scene: `/Game/Demo/Map_LivingRoom` (the dwarves on the coffee table under the window sun).

### Step 1b: discs into the GBuffer

**Status:** Not started (needs the engine to check the GBuffer / Substrate layout).

- Write discs into the GBuffer before lighting, so Lumen lights them and they receive shadows (D4).
- Fixes discs overwriting translucent surfaces (D10).
- Confirm whether discs then contribute to the HZB (D13).

### Step 2: GPU culling and indirect draws

**Status:** Not started.

- Restore the chunk-to-meshlet hierarchy: chunks keep the manifest bounds, `geometricError` and
  their level pyramid, and point to their meshlets (D12).
- Compute meshlet normal cones at load time; enable normal-cone backface culling.
- HZB occlusion culling of meshlets in the amplification shader (D13).
- GPU-built indirect arguments (`CullSplatsCS`, `BuildIndirectArgsCS`).

### Step 3: Gaussian splat mode

**Status:** Not started.

- Port the splat sort (`SplatSortCS`, `GPURadixSortCS`); one sort per view across every visible
  splat model (D14).
- Premultiplied blending; both `splatBlendSpace` modes (D2); `TemporalFilterCS` for display mode.
- Approximate depth for TSR (D3).
- Spherical harmonics; soft disc edges (D10).

### Step 4: streaming, LOD and the codec

**Status:** Not started.

- Screen-space-error traversal over the chunk LOD pyramid; dithered (8x8 Bayer) cross-fades.
- Async chunk reads from bulk data into a GPU ring buffer (D8); the codec's streaming order
  (bounding octahedron, detail grid).
- Quality controls through `r.Splats.*` and scalability (D16, proposed).

### Step 5: silhouette refinement, occlusion volume, polish

**Status:** Not started.

- Silhouette prepass and GPU edge extraction feeding streaming (readback a few frames late).
- Interior occlusion volume drawn into Unreal's depth buffer.
- Velocity for moving components (D3).

### Later

- World Partition cells for very large scans (D15).
- Editor picking (selecting the actor by clicking its discs).

## Reminders for the user

- None at the moment. (Splat shadow casting, deferred in D4, is scheduled as step 1a.)

## Open questions

- **Codec toolset** (journal, 2026-10-23): splatlab's prebuilt codec is built with MSVC 14.51, newer
  than UE 5.8's supported 14.44 and 14.50, so the public codec route doesn't link yet. Fix: publish
  the prebuilt library built with the oldest supported toolset (14.44).

- **Oct16 normal decoding in splats' shader** (D11): is `Splat.hlsl` decoding normals
  incorrectly? To be checked in the splats repo.
- **D16** (scalability integration) awaits the user's decision.
