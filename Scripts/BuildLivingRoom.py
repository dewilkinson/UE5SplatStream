# BuildLivingRoom.py
# Builds /Game/Demo/Map_LivingRoom: a furnished room of Nanite meshes (Poly Haven CC0 models fetched by
# FetchRoomAssets.ps1) with a splat scan standing on the coffee table. Imports each model as one
# combined Nanite static mesh, saves the level, captures a preview from the demo camera to
# Saved/RenderCheck/living_room.png, then quits. Run through BuildLivingRoom.ps1.

import os
import traceback

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
MODELS_DIR = os.path.join(PROJECT_DIR, "ThirdPartyAssets", "polyhaven")
OUT_DIR = os.path.join(PROJECT_DIR, "Saved", "RenderCheck")
SAMPLES_DIR = os.environ.get("SURFELS_SAMPLES", "C:/github/surfels/assets")
SPLAT = os.environ.get("ROOM_SPLAT", "dwarves")
SPLAT_HEIGHT_CM = float(os.environ.get("ROOM_SPLAT_HEIGHT", "35"))
SPLAT_EMISSIVE = float(os.environ.get("ROOM_SPLAT_EMISSIVE", "1"))
MAP = "/Game/Demo/Map_LivingRoom"
PIPELINE = "/Game/Demo/Pipelines/GLTFCombinedAssetsPipeline"

# (model, x, y, yaw) in centimetres on the floor; +X is away from the camera, +Y to its right.
# Imported Poly Haven models face +Y at yaw 0: yaw 90 faces the camera (-X), 0 faces +Y, 180 faces -Y.
FURNITURE = [
    ("sofa_03",                   230,    0,  90),
    ("modern_coffee_table_01",     80,    0,   0),
    ("mid_century_lounge_chair",  110, -200,   0),
    ("side_table_01",             230, -190,   0),
    ("vintage_oil_lamp",          230, -190,   0),   # stood on the side table below
    ("modern_wooden_cabinet",      60,  262, 180),
    ("brass_vase_01",              20,  262,   0),   # stood on the cabinet below
    ("wooden_bookshelf_worn",     265, -235,  90),
    ("potted_plant_02",           260,  200,   0),
]
STACKED = {"vintage_oil_lamp": "side_table_01", "brass_vase_01": "modern_wooden_cabinet"}

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[LivingRoom] " + msg)


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def combined_pipeline():
    # Interchange's glTF asset pipeline with "Combine All", so a multi-part model becomes one mesh.
    if not assets.does_asset_exist(PIPELINE):
        assets.duplicate_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline", PIPELINE)
    pipeline = unreal.load_asset(PIPELINE)
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    mesh.set_editor_property("build_nanite", True)
    assets.save_asset(PIPELINE)
    return [unreal.SoftObjectPath(PIPELINE + "." + PIPELINE.split("/")[-1]),
            unreal.SoftObjectPath("/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline")]


def import_model(model, pipelines):
    folder = "/Game/Demo/Furniture/" + model
    existing = [a for a in assets.list_assets(folder, recursive=True) if unreal.load_asset(a).__class__ == unreal.StaticMesh] if assets.does_directory_exist(folder) else []
    if not existing:
        source = unreal.InterchangeManager.create_source_data(os.path.join(MODELS_DIR, model, model + ".gltf").replace("\\", "/"))
        params = unreal.ImportAssetParameters()
        params.is_automated = True
        params.replace_existing = True
        params.override_pipelines = pipelines
        unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(folder, source, params)
        existing = [a for a in assets.list_assets(folder, recursive=True) if unreal.load_asset(a).__class__ == unreal.StaticMesh]
    if not existing:
        raise RuntimeError("no static mesh imported for " + model)
    mesh = unreal.load_asset(existing[0])
    nanite = mesh.get_editor_property("nanite_settings")
    if not nanite.enabled:
        nanite.enabled = True
        mesh.set_editor_property("nanite_settings", nanite)
        assets.save_loaded_asset(mesh)
    log("%s: %d mesh(es), Nanite %s, %d triangles" % (model, len(existing), mesh.get_editor_property("nanite_settings").enabled, mesh.get_num_triangles(0)))
    return mesh


def color_material(name, rgb, roughness):
    path = "/Game/Demo/Materials/" + name
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Demo/Materials", unreal.Material, unreal.MaterialFactoryNew())
    color = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
    color.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 200)
    rough.set_editor_property("r", roughness)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


def box(label, center, size, material):
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center))
    a.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    a.static_mesh_component.set_material(0, material)
    a.set_actor_label(label)
    return a


def place(mesh, label, x, y, yaw, base_z=0.0):
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, 0), unreal.Rotator(yaw=yaw))
    a.static_mesh_component.set_static_mesh(mesh)
    a.set_actor_label(label)
    origin, extent = a.get_actor_bounds(False)
    loc = a.get_actor_location()
    a.set_actor_location(unreal.Vector(loc.x, loc.y, loc.z + base_z - (origin.z - extent.z)), False, False)
    return a


def top_of(actor):
    origin, extent = actor.get_actor_bounds(False)
    return origin.z + extent.z


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    pipelines = combined_pipeline()
    meshes = {m: import_model(m, pipelines) for m, _, _, _ in FURNITURE}

    if assets.does_asset_exist(MAP):
        unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        assets.delete_asset(MAP)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level(MAP)

    # Room shell: 6 m x 6 m, 2.8 m high, with a ceiling and a 3 m x 1.6 m window in the back wall.
    # The side towards the camera stays open, like a stage set.
    floor_mat = color_material("M_Floor", (0.30, 0.20, 0.12), 0.55)
    wall_mat = color_material("M_Wall", (0.78, 0.76, 0.72), 0.9)
    box("Floor", (0, 0, -5), (700, 620, 10), floor_mat)
    box("Ceiling", (0, 0, 285), (700, 620, 10), wall_mat)
    box("Wall_Left", (0, -305, 140), (620, 10, 280), wall_mat)
    box("Wall_Right", (0, 305, 140), (620, 10, 280), wall_mat)
    box("Wall_Back_Left", (305, -228, 140), (10, 164, 280), wall_mat)    # window spans y -150..150
    box("Wall_Back_Right", (305, 228, 140), (10, 164, 280), wall_mat)
    box("Wall_Back_Sill", (305, 0, 45), (10, 300, 90), wall_mat)          # window spans z 90..250
    box("Wall_Back_Lintel", (305, 0, 265), (10, 300, 30), wall_mat)

    # Lighting: a low afternoon sun shining in through the window, the sky (captured in real time)
    # for ambient light, and a warm light in the lamp. Auto exposure adapts to the interior.
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 600), unreal.Rotator(pitch=-30, yaw=200))
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 400))
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_editor_property("intensity", 0.6)
    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 140))
    ppv.set_editor_property("unbound", True)
    settings = ppv.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    settings.set_editor_property("override_auto_exposure_min_brightness", True)
    settings.set_editor_property("auto_exposure_min_brightness", 0.5)
    settings.set_editor_property("override_auto_exposure_max_brightness", True)
    settings.set_editor_property("auto_exposure_max_brightness", 4.0)
    ppv.set_editor_property("settings", settings)

    placed = {}
    for model, x, y, yaw in FURNITURE:
        base = top_of(placed[STACKED[model]]) if model in STACKED else 0.0
        placed[model] = place(meshes[model], model, x, y, yaw, base)
    lamp = placed["vintage_oil_lamp"]
    lamp_light = actors.spawn_actor_from_class(unreal.PointLight, lamp.get_actor_location() + unreal.Vector(0, 0, 45))
    lamp_light.point_light_component.set_editor_property("light_color", unreal.Color(255, 190, 130, 255))
    lamp_light.point_light_component.set_editor_property("intensity", 40.0)
    lamp_light.point_light_component.set_editor_property("attenuation_radius", 450.0)

    # The splat scan, scaled to a figurine and stood on the coffee table.
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SAMPLES_DIR, SPLAT, SPLAT + ".sflw").replace("\\", "/")
    task.destination_path = "/Game/Demo"
    task.automated = True
    task.replace_existing = False
    task.save = True
    if not assets.does_asset_exist("/Game/Demo/" + SPLAT):
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    splat_asset = unreal.load_asset("/Game/Demo/" + SPLAT)
    table = placed["modern_coffee_table_01"]
    t_origin, _ = table.get_actor_bounds(False)
    splat = actors.spawn_actor_from_class(unreal.SurfelActor, unreal.Vector(t_origin.x, t_origin.y, 0))
    splat.set_actor_label("Splat_" + SPLAT)
    component = splat.get_editor_property("surfel_component")
    component.set_editor_property("resident_level", 0)
    component.set_surfel_asset(splat_asset)
    component.set_emissive_intensity(SPLAT_EMISSIVE)
    _, s_extent = splat.get_actor_bounds(False)
    splat.set_actor_scale3d(unreal.Vector(1, 1, 1) * (SPLAT_HEIGHT_CM / (2.0 * s_extent.z)))
    s_origin, s_extent = splat.get_actor_bounds(False)
    loc = splat.get_actor_location()
    splat.set_actor_location(unreal.Vector(loc.x + (t_origin.x - s_origin.x), loc.y + (t_origin.y - s_origin.y),
                                           loc.z + top_of(table) - (s_origin.z - s_extent.z)), False, False)

    # Demo camera: seated eye height, looking past the coffee table at the sofa.
    cam_loc = unreal.Vector(-250, 140, 150)
    target = unreal.Vector(t_origin.x + 60, -60, 55)
    rot = unreal.MathLibrary.find_look_at_rotation(cam_loc, target)
    camera = actors.spawn_actor_from_class(unreal.CameraActor, cam_loc, rot)
    camera.set_actor_label("DemoCamera")
    camera.camera_component.set_editor_property("field_of_view", 70.0)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("saved " + MAP)

    # Previews: a wide shot from the demo camera and a close-up of the coffee table.
    close_loc = unreal.Vector(t_origin.x - 95, t_origin.y - 55, top_of(table) + 35)
    close_rot = unreal.MathLibrary.find_look_at_rotation(close_loc, unreal.Vector(t_origin.x, t_origin.y, top_of(table) + 14))
    return [("living_room.png", cam_loc, rot, 70.0), ("living_room_table.png", close_loc, close_rot, 45.0)]


def capture_later(shots, frames_to_wait=600):
    # Capturing straight after import shows placeholder materials and unsettled lighting. Let the editor
    # tick until the imported materials' shaders have compiled and Lumen and auto exposure settle.
    state = {"frames": 0, "handle": None}
    caps = []
    for filename, loc, rot, fov in shots:
        cap = actors.spawn_actor_from_class(unreal.SceneCapture2D, loc, rot)
        rt = unreal.RenderingLibrary.create_render_target2d(world(), 1600, 900, unreal.TextureRenderTargetFormat.RTF_RGBA8)
        cc = cap.capture_component2d
        cc.set_editor_property("texture_target", rt)
        cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        cc.set_editor_property("capture_every_frame", True)  # every frame, so temporal effects converge
        cc.set_editor_property("fov_angle", fov)
        caps.append((filename, cap, rt))

    def tick(delta):
        state["frames"] += 1
        if state["frames"] < frames_to_wait:
            return
        unreal.unregister_slate_post_tick_callback(state["handle"])
        for filename, cap, rt in caps:
            unreal.RenderingLibrary.export_render_target(world(), rt, OUT_DIR, filename)
            actors.destroy_actor(cap)
            log("preview written to " + os.path.join(OUT_DIR, filename))
        unreal.SystemLibrary.quit_editor()

    state["handle"] = unreal.register_slate_post_tick_callback(tick)


try:
    capture_later(main())
except Exception:
    unreal.log_error("[LivingRoom] " + traceback.format_exc())
    unreal.SystemLibrary.quit_editor()
