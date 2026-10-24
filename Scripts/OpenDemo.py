# OpenDemo.py
# Runs at editor startup (see OpenDemo.ps1): opens a blank level with a sun, a sky light and a floor,
# places a sample scan as a Surfel Actor and points the viewport at it. Nothing is saved.

import os

import unreal

SAMPLES_DIR = os.environ.get("SURFELS_SAMPLES", "C:/github/surfels/assets")
NAME = os.environ.get("DEMO_SAMPLE", "dwarves")
LEVEL = int(os.environ.get("DEMO_LEVEL", "0"))
EMISSIVE = float(os.environ.get("DEMO_EMISSIVE", "2"))


def log(msg):
    unreal.log("[OpenDemo] " + msg)


def load_or_import(name):
    path = "/Game/Demo/" + name
    asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset is None:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(SAMPLES_DIR, name, name + ".sflw").replace("\\", "/")
        task.destination_path = "/Game/Demo"
        task.automated = True
        task.replace_existing = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        asset = unreal.load_asset(path)
    return asset


asset = load_or_import(NAME)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(pitch=-40, yaw=-30))
actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 600))

surfels = actors.spawn_actor_from_class(unreal.SurfelActor, unreal.Vector(0, 0, 0))
component = surfels.get_editor_property("surfel_component")
component.set_editor_property("resident_level", LEVEL)
component.set_surfel_asset(asset)
component.set_emissive_intensity(EMISSIVE)
origin, extent = surfels.get_actor_bounds(False)
radius = max(extent.x, extent.y, extent.z, 1.0)

floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(origin.x, origin.y, origin.z - extent.z))
floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
floor.set_actor_scale3d(unreal.Vector(radius * 0.06, radius * 0.06, 1))

unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    unreal.Vector(origin.x - radius * 3.2, origin.y, origin.z), unreal.Rotator())
unreal.get_editor_subsystem(unreal.EditorActorSubsystem).set_selected_level_actors([surfels])
log("placed %s (level %d, emissive %.1f); select the Surfel Actor to change its settings" % (NAME, LEVEL, EMISSIVE))
