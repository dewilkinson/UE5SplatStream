# SoundStage

A SoundStage is a scene built on a splat scan, the way a film set is built on a sound stage: the scan is the backdrop,
and the stage adds what the scan lacks — solid floors, walls, stairs and railings, a boundary to stay inside, pieces
placed in it, and where the visitor starts. Status: **concept** (first version, 2026-10-28).

## Parts

| Part | What it is |
| --- | --- |
| Scene spec (`*.soundstage.json`) | The editable source: plain JSON, package frame, asset paths relative to a root |
| `USoundStageAsset` | The spec as a UE asset (New › SoundStage, or drag a `.soundstage.json` into the Content Browser) |
| `ASoundStageActor` | Realises an asset in a level: backdrop, one shape per volume, one pedestal per piece |
| `ASplatPakPedestal` | A sculpture on a plinth with a key light and optional uplight; what each piece is placed with |

## The scene spec

```json
{
  "soundstage": 1,
  "backdrop": "bishops_palace",
  "start": { "eye": [0.61, -0.92, 11.09], "look": [-0.08, 0.27, -0.96] },
  "volumes": [
    { "name": "CentreStairs", "kind": "stairs", "shape": "box",
      "center": [0.2, -1.36, 6.85], "size": [8.86, 0.2, 2.4], "yaw": -90, "pitch": 16.5 }
  ],
  "pieces": [
    { "name": "Oligarchy", "sculpture": "oligarchy", "floor": [2.62, 3.60, 14.96], "facing": -151.9,
      "height": 1.2, "plinth": "SM_Plinth_100_60", "plinth_emissive": 0.3, "uplight": true }
  ]
}
```

- **Frame**: the backdrop package's own, metres, Y up (SplatPak's), so SplatPak can read the same file. UE maps a point
  (x, y, z) to (x, z, y) × 100 cm in the stage actor's frame.
- **Paths**: every asset (backdrop, sculptures, plinths) is a path relative to the stage's root directory, so the file
  stays readable and hand-editable. In UE the root is `RootDirectory` (a content path), or the folder the stage asset
  is in when that is empty; `..` is allowed. Inside UE the resolved links are ordinary object references.
- **Volumes**: `kind` is `floor`, `wall`, `stairs`, `railing`, `boundary` or `trigger`; `shape` is `box`, `sphere` or
  `capsule`. `size` is the full size along the shape's own axes: X along its heading, Y up, Z across (a sphere uses X as
  its diameter; a capsule X as its diameter and Y as its height). `yaw` is the heading in degrees from +X toward +Z;
  `pitch` raises the +X end (a flight of stairs climbing along its heading).
- **Pieces**: `floor` is the point the piece stands on; `facing` a heading in degrees (or a direction `[x, z]`);
  `height` the sculpture's height in metres; `plinth` optional; `front_offset` corrects a scan whose front is not +X.

## In a level

| Kind | Collision | Drawn |
| --- | --- | --- |
| floor, wall, stairs, railing | blocks | green, white, orange, yellow wireframe |
| boundary | a 20 cm slab outside each face of its box blocks from within | red |
| trigger | overlaps only (overlap events on) | cyan |

The wireframes show in the editor, and in play with **Show Volumes In Game**. The actor rebuilds when its asset changes.

## Accessors

- Asset: `FindVolume`, `GetVolumesOfKind`, `SetVolume`, `RemoveVolume`, `FindPiece`, `SetPiece`, `RemovePiece`;
  `GetRoot`, `ResolvePath`, `MakeRelativePath`, `ResolveAssets`; `ImportSpec(File)`, `ExportSpec(File)`;
  `PackageToStage`, `StageToPackage`, `GetVolumeTransform`.
- Actor: `Rebuild`, `GetVolumeComponent`, `GetPiece`, `GetPieceNames`, `GetVolumesAt`, `IsInsideBoundary`,
  `GetStartTransform`, `PackageToWorld`, `WorldToPackage`, `SetVolumesVisible`.

## Rendering

A stage has no scene proxy of its own: each part brings the engine's or the plugin's (the backdrop and sculptures
`FSplatPakSceneProxy`, the plinths Nanite, the volumes the shape components' wireframe proxies). Where one would earn its
place is composition: a stage-level proxy that draws all its splats in one sorted pass under one streaming budget, so
overlapping splats from different scans blend correctly. That is the next design step, not part of the concept.

## Not yet

- Volumes are hand-placed (the palace's from measured floor heights); seeding them from SplatPak's walk grid is next.
- SplatPak reading the spec (collision for its walk mode, the pieces, the start).
- Gizmo editing of volumes in the viewport; today they are edited in the Details panel or the spec.
