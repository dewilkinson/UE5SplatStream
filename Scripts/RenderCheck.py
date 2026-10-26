# RenderCheck.py
# Runs inside the editor (see RenderCheck.ps1). Imports sample .gstx packages, places each in a test
# level with a light and an occluding cube, and captures it through a SceneCapture2D on the mesh-shader
# path, the vertex-shader fallback, and with the model hidden (baseline). Writes PNGs and a JSON
# summary to Saved/RenderCheck, then quits the editor.

import json
import os
import sys
import traceback

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT_DIR = os.path.join(PROJECT_DIR, "Saved", "RenderCheck")
SAMPLES_DIR = os.environ.get("SPLATPAK_SAMPLES", "C:/github/3DGSStream/assets")
# Options (set by RenderCheck.ps1): which samples, which LOD level (-1 = coarsest), capture size,
# extra captures at these emissive intensities, and whether to place the occluding cube.
SAMPLES = [s for s in os.environ.get("RENDERCHECK_SAMPLES", "dwarves,venus").split(",") if s]
LEVEL = int(os.environ.get("RENDERCHECK_LEVEL", "-1"))
WIDTH, HEIGHT = (int(v) for v in os.environ.get("RENDERCHECK_RES", "1280x720").lower().split("x"))
EMISSIVE = [float(v) for v in os.environ.get("RENDERCHECK_EMISSIVE", "").split(",") if v]
OCCLUDER = os.environ.get("RENDERCHECK_OCCLUDER", "1") != "0"
# Shadow mesh checks (-Shadow): a floor under the model seen from above, captured with the shadow mesh casting and not,
# and the mesh distance field view with Affect Lumen on and off
SHADOW = os.environ.get("RENDERCHECK_SHADOW", "0") == "1"

results = {"samples": {}, "errors": []}


def log(msg):
    unreal.log("[RenderCheck] " + msg)


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def import_sample(name):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SAMPLES_DIR, name, name + ".gstx").replace("\\", "/")
    task.destination_path = "/Game/RenderCheck"
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = unreal.load_asset("/Game/RenderCheck/" + name)
    if asset is None:
        raise RuntimeError("import of %s produced no asset" % task.filename)
    log("imported %s: format %s, %d chunks, coarsest level %d" % (
        name, asset.get_editor_property("record_format"), asset.get_editor_property("num_chunks"),
        asset.get_editor_property("coarsest_level")))
    return asset


def capture(capture_actor, render_target, filename):
    capture_actor.capture_component2d.capture_scene()
    unreal.RenderingLibrary.export_render_target(world(), render_target, OUT_DIR, filename)
    return os.path.join(OUT_DIR, filename)


def check_sample(name):
    asset = import_sample(name)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Fresh level per sample: a directional light and a sky light so the scene isn't black.
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level("/Game/RenderCheck/Map_" + name)
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(pitch=-40, yaw=-30))
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 600))

    splats = actors.spawn_actor_from_class(unreal.SplatActor, unreal.Vector(0, 0, 0))
    component = splats.get_editor_property("splat_component")
    component.set_editor_property("resident_level", LEVEL)
    component_asset[0] = asset
    if os.environ.get("RENDERCHECK_NO_SPLATS") != "1":  # debugging: same scene, nothing for the plugin to draw
        component.set_splat_asset(asset)
    origin, extent = splats.get_actor_bounds(False)
    radius = max(extent.x, extent.y, extent.z, 1.0)
    log("%s bounds: origin %s extent %s" % (name, origin, extent))

    # Camera on -X looking at the model; a cube between them covering the left part of the view.
    distance = radius * 3.2
    cam_pos = unreal.Vector(origin.x - distance, origin.y, origin.z)
    cap = actors.spawn_actor_from_class(unreal.SceneCapture2D, cam_pos, unreal.Rotator())
    rt = unreal.RenderingLibrary.create_render_target2d(world(), WIDTH, HEIGHT, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cc = cap.capture_component2d
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cc.set_editor_property("capture_every_frame", False)
    cc.set_editor_property("capture_on_movement", False)
    cc.set_editor_property("always_persist_rendering_state", True)  # temporal effects (TSR, Lumen, exposure) need view history
    cc.set_editor_property("fov_angle", 60.0)

    if OCCLUDER:
        cube = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(origin.x - distance * 0.45, origin.y - radius * 0.55, origin.z))
        cube.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
        cube.set_actor_scale3d(unreal.Vector(0.2, radius * 0.006, radius * 0.02))

    shots = {}
    for label, cvar in (("mesh", "r.SplatPak.MeshShaders 1"), ("fallback", "r.SplatPak.MeshShaders 0")):
        unreal.SystemLibrary.execute_console_command(world(), cvar)
        # Two captures: the first can still be uploading buffers or compiling PSOs.
        capture(cap, rt, "%s_%s_warmup.png" % (name, label))
        shots[label] = capture(cap, rt, "%s_%s.png" % (name, label))
    unreal.SystemLibrary.execute_console_command(world(), "r.SplatPak.MeshShaders 1")

    for intensity in EMISSIVE:
        component.set_emissive_intensity(intensity)
        capture(cap, rt, "%s_emissive%g_warmup.png" % (name, intensity))
        shots["emissive%g" % intensity] = capture(cap, rt, "%s_emissive%g.png" % (name, intensity))
    component.set_emissive_intensity(1.0)

    if SHADOW:
        shots.update(check_shadow(name, actors, component, origin, extent, radius, cap, rt))

    splats.set_actor_hidden_in_game(True)
    component.set_visibility(False)
    shots["hidden"] = capture(cap, rt, "%s_hidden.png" % name)

    results["samples"][name] = {
        "record_format": asset.get_editor_property("record_format"),
        "bounds_origin": [origin.x, origin.y, origin.z],
        "bounds_extent": [extent.x, extent.y, extent.z],
        "shots": shots,
    }


def check_shadow(name, actors, component, origin, extent, radius, cap, rt):
    shots = {}
    mesh = component.get_shadow_mesh_component()
    log("%s shadow mesh component: %s" % (name, "present" if mesh else "none"))
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(origin.x, origin.y, origin.z - extent.z - 1.0))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    floor.set_actor_scale3d(unreal.Vector(radius * 0.08, radius * 0.08, 1.0))
    # From above and to the side, so the floor around the model fills the view
    cam = unreal.Vector(origin.x - radius * 2.2, origin.y - radius * 1.2, origin.z + radius * 2.4)
    cap.set_actor_location(cam, False, False)
    cap.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam, unreal.Vector(origin.x, origin.y + radius * 0.6, origin.z - extent.z)), False)
    for label, cast in (("shadow", True), ("shadow_off", False)):
        component.set_editor_property("shadow_mesh_cast_shadow", cast)
        component.set_splat_asset(None)
        component.set_splat_asset(component_asset[0])
        capture(cap, rt, "%s_%s_warmup.png" % (name, label))
        shots[label] = capture(cap, rt, "%s_%s.png" % (name, label))
    component.set_editor_property("shadow_mesh_cast_shadow", True)
    # Mesh distance fields (Lumen), with Affect Lumen on and off
    cc = cap.capture_component2d
    cc.set_editor_property("show_flag_settings", [unreal.EngineShowFlagsSetting(show_flag_name="VisualizeMeshDistanceFields", enabled=True)])
    component.set_visibility(False, False)  # Splats off, so only the (hidden) shadow mesh's field can show where the model is
    for label, affect in (("distancefield", True), ("distancefield_off", False)):
        component.set_editor_property("shadow_mesh_affect_lumen", affect)
        component.set_splat_asset(None)
        component.set_splat_asset(component_asset[0])
        capture(cap, rt, "%s_%s_warmup.png" % (name, label))
        shots[label] = capture(cap, rt, "%s_%s.png" % (name, label))
    cc.set_editor_property("show_flag_settings", [])
    component.set_visibility(True, False)
    component.set_editor_property("shadow_mesh_affect_lumen", True)
    floor.destroy_actor()
    return shots


component_asset = [None]


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name in SAMPLES:
        try:
            check_sample(name)
        except Exception:
            results["errors"].append("%s: %s" % (name, traceback.format_exc()))
            unreal.log_error("[RenderCheck] " + traceback.format_exc())
    with open(os.path.join(OUT_DIR, "results.json"), "w") as f:
        json.dump(results, f, indent=2)
    log("done; results in " + OUT_DIR)
    unreal.SystemLibrary.quit_editor()


main()
