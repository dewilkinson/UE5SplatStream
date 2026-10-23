# Surfels UE plugin: design decisions

Agreed 2026-10-23. See the architecture plan (view extension + RDG passes, scene proxy owns GPU state).

1. **Colour space: keep `splatBlendSpace`.**
   - `1` linear: drawn in `PrePostProcessPass_RenderThread`, sRGB->linear, multiplied by `View.PreExposure`, before TSR.
   - `0` display: drawn after the tonemapper (`SubscribeToPostProcessingPass`), matches the reference viewer.
     Runs after TSR, so it gets no TSR and its approx depth cannot help TSR. Port `TemporalFilterCS` for this mode only.
     Post-tonemap targets are at output resolution; scene depth is at render resolution, so rescale UVs when depth-testing.
2. **TSR: write approximate depth.** Splats write depth into SceneDepth where splat alpha exceeds a threshold
   (nearest "solid" splat, LESS test), in the pre-post-process pass, i.e. after lighting and before TSR.
   Never earlier: Lumen/SSAO would read it as a surface with no GBuffer. Static models get camera-motion
   reprojection from depth alone; moving components will also need velocity.
3. **Lighting:** Gaussian splats unlit (baked lighting); opaque discs written to GBuffer so Lumen lights them
   and they receive shadows. **TODO (remind user): shadow casting needs an extra depth pass per light. Deferred.**
4. **Mesh shaders wherever possible.** Amplification + mesh shader path is the default whenever the platform
   and GPU support it; the instanced vertex-shader path is the fallback, not the baseline.
   - Compile the mesh permutation only where `FDataDrivenShaderPlatformInfo::GetSupportsMeshShadersTier1` is true.
   - Pick at runtime from `GRHISupportsMeshShadersTier1`, with a `r.Surfels.MeshShaders` cvar to force the fallback.
   - Project targets DX12 / SM6 (Config/DefaultEngine.ini).
   - Build both paths from the start so they stay in parity, but verify the mesh path first.
