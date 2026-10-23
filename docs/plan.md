# Surfels UE plugin: plan and status

The build order and where each step stands. Decisions referenced as D*n* are in
[decisions.md](decisions.md); dated progress and verification are in [journal.md](journal.md).

**Status values:** *Not started*, *In progress*, *Written, not compiled* (code exists but hasn't
been built against Unreal), *Done* (built, tested, and seen working in the engine).

## Prerequisites

| Item | Status |
|---|---|
| Visual Studio 2022 17.14 with the C++ game workloads, MSVC 14.44, Windows SDKs 10.0.22621 / 26100 | Done |
| Plugin sandbox project (`PluginSandbox`) with build, test and package scripts | Done |
| surfels repo cloned with LFS assets and the Cauldron submodule (`C:\github\surfels`) | Done |
| bluesec-codec builds and stages into the plugin (D7) | Done |
| Unreal Engine 5.8 installed from the Epic Games Launcher | In progress (downloading) |
| First build of the plugin against the installed engine | Not started (needs the engine) |

## Build order

### Step 1: one resident level as opaque discs

**Status:** Written, not compiled. Staged in git, not committed (waiting on the user).

- `.sflw` import into a `USurfelAsset` (D8); `USurfelComponent` / `ASurfelActor`; scene proxy.
- One level resident (coarsest by default), split into 64-surfel meshlets.
- Opaque discs after lighting, depth-tested and depth-writing (D10, interim).
- Mesh-shader path by default with the vertex fallback (D5); amplification-shader frustum culling.
- Both record formats: 8-byte surfels and 20-byte splat records (drawn as discs, DC colour).
- Tests: synthetic package round-trips and corruption checks; all bundled scans decode in bounds.

**Remaining:** first compile against the installed engine (the UE APIs most likely to need
adjusting are listed in `Plugins/Surfels/README.md`), then see it render.

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
- GPU-built indirect arguments (`CullSurfelsCS`, `BuildIndirectArgsCS`).

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
- Quality controls through `r.Surfels.*` and scalability (D16, proposed).

### Step 5: silhouette refinement, occlusion volume, polish

**Status:** Not started.

- Silhouette prepass and GPU edge extraction feeding streaming (readback a few frames late).
- Interior occlusion volume drawn into Unreal's depth buffer.
- Velocity for moving components (D3).

### Later

- World Partition cells for very large scans (D15).
- Splat shadow casting: an extra depth pass per light (D4; see Reminders).
- Editor picking (selecting the actor by clicking its discs).

## Reminders for the user

- **Splat shadow casting** was deferred on purpose (D4). Raise it again once steps 1 to 3 work,
  or whenever lighting comes up.

## Open questions

- **Oct16 normal decoding in surfels' shader** (D11): is `Surfels.hlsl` decoding normals
  incorrectly? To be checked in the surfels repo.
- **D16** (scalability integration) awaits the user's decision.
