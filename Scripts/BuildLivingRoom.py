# BuildLivingRoom.py
# Builds /Game/Demo/Map_LivingRoom: a furnished room of Nanite meshes (Poly Haven CC0 models fetched by
# FetchRoomAssets.ps1) with a splat scan standing on the coffee table. Imports each model as one
# combined Nanite static mesh, saves the level, captures a preview from the demo camera to
# Saved/RenderCheck/living_room.png, then quits. Run through BuildLivingRoom.ps1.

import os
import traceback

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
MODELS_DIR = os.environ.get("ROOM_ASSETS_DIR", os.path.join(PROJECT_DIR, "ThirdPartyAssets", "polyhaven"))
OUT_DIR = os.path.join(PROJECT_DIR, "Saved", "RenderCheck")
SAMPLES_DIR = os.environ.get("SURFELS_SAMPLES", "C:/github/surfels/assets")
SPLAT = os.environ.get("ROOM_SPLAT", "dwarves")
SPLAT_HEIGHT_CM = float(os.environ.get("ROOM_SPLAT_HEIGHT", "35"))
SPLAT_EMISSIVE = float(os.environ.get("ROOM_SPLAT_EMISSIVE", "1"))
MAP = "/Game/Demo/Map_LivingRoom"
PIPELINE = "/Game/Demo/Pipelines/GLTFCombinedAssetsPipeline"
FLAMES = os.environ.get("ROOM_FLAMES", "/Game/ExampleContent/Blueprint_Communication/Effects/P_Flames")

# (model, x, y, yaw) in centimetres on the floor; +X is away from the camera, +Y to its right.
# Imported Poly Haven models face +Y at yaw 0: yaw 90 faces the camera (-X), 0 faces +Y, 180 faces -Y.
FURNITURE = [
    ("sofa_03",                   230,    0,  90),
    ("modern_coffee_table_01",     80,    0,   0),
    ("mid_century_lounge_chair",  140, -175,   0),
    ("side_table_01",             230, -190,   0),
    ("vintage_oil_lamp",          230, -190,   0),   # stood on the side table below
    ("drawer_cabinet",             60,  270, 180),
    ("brass_vase_01",              60,  270,   0),   # stood on the cabinet below
    ("wooden_bookshelf_worn",     265, -235,  90),
    ("potted_plant_02",           260,  200,   0),
]
STACKED = {"vintage_oil_lamp": "side_table_01", "brass_vase_01": "drawer_cabinet"}
# Hung from the ceiling instead of stood on the floor: (model, x, y).
HUNG = [("Chandelier_01", 60, 0)]
# Models with translucent materials (the sofa's fringe, the lamp's glass and flame), which Nanite
# can't draw; they stay ordinary static meshes. Every other model is Nanite.
NOT_NANITE = {"sofa_03", "vintage_oil_lamp"}
CEILING_Z = 280.0

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
    # Usually one combined mesh; if the importer leaves several parts, all of them are placed together.
    meshes = [unreal.load_asset(path) for path in existing]
    want_nanite = model not in NOT_NANITE
    for mesh in meshes:
        nanite = mesh.get_editor_property("nanite_settings")
        if nanite.enabled != want_nanite:
            nanite.enabled = want_nanite
            mesh.set_editor_property("nanite_settings", nanite)
            assets.save_loaded_asset(mesh)
    log("%s: %d mesh(es), Nanite %s" % (model, len(meshes), want_nanite))
    return meshes


def import_texture(path, normal):
    folder = "/Game/Demo/Textures"
    name = os.path.splitext(os.path.basename(path))[0]
    if not assets.does_asset_exist(folder + "/" + name):
        task = unreal.AssetImportTask()
        task.filename = path.replace("\\", "/")
        task.destination_path = folder
        task.automated = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(folder + "/" + name)
    if normal:
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("srgb", False)
    elif name.endswith("_rough"):
        texture.set_editor_property("srgb", False)
    assets.save_loaded_asset(texture)
    return texture


def textured_material(name, texture_id, tiling, roughness_scale=1.0):
    # Colour, DirectX normal and roughness maps from a Poly Haven texture, tiled `tiling` times per face.
    # roughness_scale multiplies the roughness map (above 1 dulls a glossy surface).
    path = "/Game/Demo/Materials/" + name
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    base = os.path.join(MODELS_DIR, "textures", texture_id, texture_id)
    mel = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Demo/Materials", unreal.Material, unreal.MaterialFactoryNew())
    uv = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -900, 200)
    uv.set_editor_property("u_tiling", tiling)
    uv.set_editor_property("v_tiling", tiling)
    maps = [("_diff.jpg", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "RGB", unreal.MaterialProperty.MP_BASE_COLOR, False),
            ("_nor_dx.jpg", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "RGB", unreal.MaterialProperty.MP_NORMAL, True),
            ("_rough.jpg", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, "R", unreal.MaterialProperty.MP_ROUGHNESS, False)]
    for i, (suffix, sampler, output, prop, normal) in enumerate(maps):
        sample = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -500, i * 250)
        sample.set_editor_property("texture", import_texture(base + suffix, normal))
        sample.set_editor_property("sampler_type", sampler)
        mel.connect_material_expressions(uv, "", sample, "UVs")
        if prop == unreal.MaterialProperty.MP_ROUGHNESS and roughness_scale != 1.0:
            scale = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, i * 250)
            scale.set_editor_property("const_b", roughness_scale)
            mel.connect_material_expressions(sample, output, scale, "A")
            mel.connect_material_property(scale, "", prop)
        else:
            mel.connect_material_property(sample, output, prop)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


def flat_material(name, rgb, roughness):
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


def emissive_material(name, rgb, strength):
    # A glowing surface (the fireplace embers). Lumen treats emissive surfaces as light sources.
    path = "/Game/Demo/Materials/" + name
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Demo/Materials", unreal.Material, unreal.MaterialFactoryNew())
    color = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
    color.set_editor_property("constant", unreal.LinearColor(rgb[0] * strength, rgb[1] * strength, rgb[2] * strength, 1.0))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    base = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 200)
    base.set_editor_property("constant", unreal.LinearColor(0.02, 0.01, 0.005, 1.0))
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


def log_piece(label, center, length, radius, yaw, material):
    # A log lying on its side: the engine cylinder (axis Z, 100 cm) scaled and tipped onto X, then turned.
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center), unreal.Rotator(pitch=90, yaw=yaw))
    a.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cylinder"))
    a.set_actor_scale3d(unreal.Vector(radius / 50.0, radius / 50.0, length / 100.0))
    a.static_mesh_component.set_material(0, material)
    a.set_actor_label(label)
    return a


def build_fireplace(center_x, wall_y, brick, plaster):
    # A brick fireplace against the wall at wall_y (facing +Y): surround, mantel shelf, hearth,
    # a dark firebox, glowing embers and a warm fire light.
    w, h, d = 160.0, 125.0, 45.0          # surround width, height, depth (cm)
    ow, oh = 84.0, 70.0                   # firebox opening
    y = wall_y + d / 2.0
    jamb = (w - ow) / 2.0
    box("Fireplace_JambL", (center_x - (ow + jamb) / 2.0, y, oh / 2.0 + 8), (jamb, d, oh + 16), brick)
    box("Fireplace_JambR", (center_x + (ow + jamb) / 2.0, y, oh / 2.0 + 8), (jamb, d, oh + 16), brick)
    box("Fireplace_Header", (center_x, y, (oh + 16 + h) / 2.0), (w, d, h - oh - 16), brick)
    mantel = box("Fireplace_Mantel", (center_x, y + 6, h + 4), (w + 24, d + 14, 8), plaster)
    box("Fireplace_Hearth", (center_x, wall_y + d + 22, 4), (w + 30, 44, 8), brick)
    # Firebox: sooty, near-black walls, so the fire lights the room rather than its own box.
    soot = flat_material("M_Soot", (0.02, 0.018, 0.016), 0.95)
    box("Fireplace_Back", (center_x, wall_y + 6, oh / 2.0 + 8), (ow, 12, oh + 16), soot)
    box("Fireplace_Floor", (center_x, y, 6), (ow, d, 4), soot)
    box("Fireplace_SideL", (center_x - ow / 2.0 + 2, y, oh / 2.0 + 8), (4, d, oh + 16), soot)
    box("Fireplace_SideR", (center_x + ow / 2.0 - 2, y, oh / 2.0 + 8), (4, d, oh + 16), soot)
    box("Fireplace_Roof", (center_x, y, oh + 14), (ow, d, 4), soot)

    # A settled fire: three charred logs on a bed of scattered embers (no flame effect).
    bark = flat_material("M_CharredLog", (0.035, 0.025, 0.018), 0.9)
    log_piece("Fireplace_Log1", (center_x - 4, y - 6, 15), 58, 6.5, 8, bark)
    log_piece("Fireplace_Log2", (center_x + 3, y + 7, 15), 54, 6.0, -6, bark)
    log_piece("Fireplace_Log3", (center_x, y + 1, 26), 50, 5.5, 62, bark)
    hot = emissive_material("M_Embers_Hot", (1.0, 0.30, 0.05), 6.0)
    warm = emissive_material("M_Embers_Warm", (0.9, 0.18, 0.03), 2.5)
    for i, (dx, dy, sx, sy, mat) in enumerate([(-18, 2, 14, 9, hot), (6, -2, 18, 7, hot), (22, 6, 9, 8, warm),
                                               (-6, 10, 12, 6, warm), (14, -9, 8, 6, warm), (-26, -8, 7, 6, warm)]):
        box("Fireplace_Ember%d" % i, (center_x + dx, y + dy, 9), (sx, sy, 2), mat)

    # Flames, when the project has a flame effect (the demo project migrates P_Flames from Epic's
    # Content Examples; it isn't redistributable, so this repository doesn't carry it).
    if assets.does_asset_exist(FLAMES):
        flames = actors.spawn_actor_from_object(unreal.load_asset(FLAMES), unreal.Vector(center_x, y + 1, 16))
        flames.set_actor_scale3d(unreal.Vector(0.32, 0.32, 0.32))
        flames.set_actor_label("Fireplace_Flames")
        log("flames: " + FLAMES)

    # The fire light sits low, at the front of the opening, so it falls on the hearth and the room.
    fire = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(center_x, wall_y + d - 4, 22))
    fire.set_actor_label("Fireplace_Light")
    light = fire.point_light_component
    light.set_editor_property("light_color", unreal.Color(r=255, g=150, b=80, a=255))
    light.set_editor_property("intensity", 20.0)
    light.set_editor_property("source_radius", 12.0)
    light.set_editor_property("attenuation_radius", 900.0)
    return mantel


def place_splat(name, on_actor, height_cm, level, emissive, offset=(0.0, 0.0)):
    # Imports a sample scan if needed, scales it to height_cm and stands it on top of on_actor.
    if not assets.does_asset_exist("/Game/Demo/" + name):
        task = unreal.AssetImportTask()
        task.filename = os.path.join(SAMPLES_DIR, name, name + ".sflw").replace("\\", "/")
        task.destination_path = "/Game/Demo"
        task.automated = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    base_origin, _ = on_actor.get_actor_bounds(False)
    target = unreal.Vector(base_origin.x + offset[0], base_origin.y + offset[1], top_of(on_actor))
    splat = actors.spawn_actor_from_class(unreal.SurfelActor, unreal.Vector(target.x, target.y, 0))
    splat.set_actor_label("Splat_" + name)
    component = splat.get_editor_property("surfel_component")
    component.set_editor_property("resident_level", level)
    component.set_surfel_asset(unreal.load_asset("/Game/Demo/" + name))
    component.set_emissive_intensity(emissive)
    _, extent = splat.get_actor_bounds(False)
    splat.set_actor_scale3d(unreal.Vector(1, 1, 1) * (height_cm / (2.0 * extent.z)))
    origin, extent = splat.get_actor_bounds(False)
    loc = splat.get_actor_location()
    splat.set_actor_location(unreal.Vector(loc.x + target.x - origin.x, loc.y + target.y - origin.y,
                                           loc.z + target.z - (origin.z - extent.z)), False, False)
    log("placed %s: level %d, %.0f cm, emissive %.1f" % (name, level, height_cm, emissive))
    return splat


def box(label, center, size, material):
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center))
    a.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    a.static_mesh_component.set_material(0, material)
    a.set_actor_label(label)
    return a


def place(meshes, label, x, y, yaw, base_z=0.0):
    # One actor per mesh part, all at the same transform; extra parts are attached to the first, so
    # moving it moves the whole model. The model is then stood with its lowest point at base_z.
    parts = []
    for i, mesh in enumerate(meshes):
        a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, 0), unreal.Rotator(yaw=yaw))
        a.static_mesh_component.set_static_mesh(mesh)
        a.set_actor_label(label if i == 0 else "%s_part%d" % (label, i))
        parts.append(a)
    rule = unreal.AttachmentRule.KEEP_WORLD
    for a in parts[1:]:
        a.attach_to_actor(parts[0], "", rule, rule, rule, False)
    lo, _ = z_range(parts[0])
    loc = parts[0].get_actor_location()
    parts[0].set_actor_location(unreal.Vector(loc.x, loc.y, loc.z + base_z - lo), False, False)
    return parts[0]


def z_range(actor):
    # Lowest and highest point of an actor together with any parts attached to it.
    lo, hi = None, None
    for a in [actor] + list(actor.get_attached_actors()):
        origin, extent = a.get_actor_bounds(False)
        lo = origin.z - extent.z if lo is None else min(lo, origin.z - extent.z)
        hi = origin.z + extent.z if hi is None else max(hi, origin.z + extent.z)
    return lo, hi


def top_of(actor):
    return z_range(actor)[1]


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    pipelines = combined_pipeline()
    meshes = {m: import_model(m, pipelines) for m, _, _, _ in FURNITURE}
    meshes.update({m: import_model(m, pipelines) for m, _, _ in HUNG})

    # Rebuild in place: open the existing level and clear it (deleting it fails while it's still
    # referenced in memory, and new_level then refuses the path), or create it the first time.
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if assets.does_asset_exist(MAP):
        level_editor.load_level(MAP)
        for actor in actors.get_all_level_actors():
            if not isinstance(actor, (unreal.WorldSettings, unreal.Brush)):
                actors.destroy_actor(actor)
    else:
        level_editor.new_level(MAP)
    level_path = world().get_path_name()
    if not level_path.startswith(MAP + "."):
        raise RuntimeError("expected to be editing %s but the open level is %s; is it open in another editor?" % (MAP, level_path))

    # Room shell: a closed 6 m x 6 m room, 2.8 m high, lit through a 3 m x 1.6 m window in the back
    # wall. Herringbone parquet floor, painted plaster walls and ceiling (Poly Haven, CC0).
    floor_mat = textured_material("M_Parquet_Satin", "herringbone_parquet", 4.0, roughness_scale=2.0)
    wall_mat = textured_material("M_Plaster", "painted_plaster_wall", 3.0)
    box("Floor", (0, 0, -5), (620, 620, 10), floor_mat)
    box("Ceiling", (0, 0, 285), (620, 620, 10), wall_mat)
    box("Wall_Left", (0, -305, 140), (620, 10, 280), wall_mat)
    box("Wall_Right", (0, 305, 140), (620, 10, 280), wall_mat)
    box("Wall_Front", (-305, 0, 140), (10, 620, 280), wall_mat)
    # Outside: a lawn to the horizon (1 km) and light height fog, so the window doesn't look onto a void.
    box("Ground_Outside", (50300, 0, -6), (100000, 100000, 10), flat_material("M_Lawn", (0.09, 0.16, 0.05), 0.9))
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
    fog.component.set_editor_property("fog_density", 0.004)
    box("Wall_Back_Left", (305, -228, 140), (10, 164, 280), wall_mat)    # window spans y -150..150
    box("Wall_Back_Right", (305, 228, 140), (10, 164, 280), wall_mat)
    box("Wall_Back_Sill", (305, 0, 45), (10, 300, 90), wall_mat)          # window spans z 90..250
    box("Wall_Back_Lintel", (305, 0, 265), (10, 300, 30), wall_mat)

    # Lighting, all dynamic: a low afternoon sun shining in through the window onto the floor, the
    # sky (captured in real time) for window light, Lumen bouncing both around the room, and a warm
    # light in the lamp. Exposure is held in a narrow range so the interior keeps its contrast.
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 600), unreal.Rotator(pitch=-25, yaw=190))
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sun.light_component.set_editor_property("intensity", 8.0)
    sun.light_component.set_editor_property("light_source_angle", 0.8)
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 400))
    sky.light_component.set_editor_property("real_time_capture", True)
    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 140))
    ppv.set_editor_property("unbound", True)
    settings = ppv.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    settings.set_editor_property("override_auto_exposure_min_brightness", True)
    settings.set_editor_property("auto_exposure_min_brightness", 0.8)   # a narrow range: a camera with near-fixed exposure,
    settings.set_editor_property("override_auto_exposure_max_brightness", True)
    settings.set_editor_property("auto_exposure_max_brightness", 1.2)   # so shadows stay dark instead of being lifted to daylight
    settings.set_editor_property("override_vignette_intensity", True)
    settings.set_editor_property("vignette_intensity", 0.3)
    ppv.set_editor_property("settings", settings)

    placed = {}
    for model, x, y, yaw in FURNITURE:
        base = top_of(placed[STACKED[model]]) if model in STACKED else 0.0
        placed[model] = place(meshes[model], model, x, y, yaw, base)
    for model, x, y in HUNG:
        hung = place(meshes[model], model, x, y, 0)
        origin, extent = hung.get_actor_bounds(False)
        loc = hung.get_actor_location()
        hung.set_actor_location(unreal.Vector(loc.x, loc.y, loc.z + CEILING_Z - (origin.z + extent.z)), False, False)
        placed[model] = hung
    chandelier = placed["Chandelier_01"]
    c_mid, _ = chandelier.get_actor_bounds(False)
    bulbs = actors.spawn_actor_from_class(unreal.PointLight, c_mid)
    bulbs.set_actor_label("Chandelier_Light")
    bulbs.point_light_component.set_editor_property("light_color", unreal.Color(r=255, g=214, b=170, a=255))
    bulbs.point_light_component.set_editor_property("intensity", 35.0)
    bulbs.point_light_component.set_editor_property("source_radius", 20.0)
    bulbs.point_light_component.set_editor_property("attenuation_radius", 1000.0)

    mantel = build_fireplace(-20.0, -300.0, textured_material("M_Brick", "brown_brick_02", 1.5), wall_mat)

    lamp = placed["vintage_oil_lamp"]
    lamp_light = actors.spawn_actor_from_class(unreal.PointLight, lamp.get_actor_location() + unreal.Vector(0, 0, 45))
    lamp_light.point_light_component.set_editor_property("light_color", unreal.Color(r=255, g=190, b=130, a=255))
    lamp_light.point_light_component.set_editor_property("intensity", 40.0)
    lamp_light.point_light_component.set_editor_property("attenuation_radius", 450.0)

    # The splat scans, scaled to figurines: the main one on the coffee table, Cthulhu on the mantel
    # (level 1, 1.66 M surfels, which suits small GPUs better than level 0's 3.3 M).
    table = placed["modern_coffee_table_01"]
    t_origin, _ = table.get_actor_bounds(False)
    place_splat(SPLAT, table, SPLAT_HEIGHT_CM, 0, SPLAT_EMISSIVE)
    cthulhu = place_splat("cthulu", mantel, 40.0, 1, SPLAT_EMISSIVE, offset=(0.0, -6.0))
    c_origin, c_extent = cthulhu.get_actor_bounds(False)

    # Demo camera: seated eye height, looking past the coffee table at the sofa.
    cam_loc = unreal.Vector(-250, 140, 150)
    target = unreal.Vector(t_origin.x + 60, -60, 55)
    rot = unreal.MathLibrary.find_look_at_rotation(cam_loc, target)
    camera = actors.spawn_actor_from_class(unreal.CameraActor, cam_loc, rot)
    camera.set_actor_label("DemoCamera")
    camera.camera_component.set_editor_property("field_of_view", 70.0)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    if not level_editor.save_current_level():
        raise RuntimeError("saving %s failed" % MAP)
    log("saved " + MAP)

    # Previews: a wide shot from the demo camera and a close-up of the coffee table.
    close_loc = unreal.Vector(t_origin.x - 95, t_origin.y - 55, top_of(table) + 35)
    close_rot = unreal.MathLibrary.find_look_at_rotation(close_loc, unreal.Vector(t_origin.x, t_origin.y, top_of(table) + 14))
    cth_loc = unreal.Vector(c_origin.x - 55, c_origin.y + 110, c_origin.z + 10)   # from the room, facing the mantel
    cth_rot = unreal.MathLibrary.find_look_at_rotation(cth_loc, c_origin)
    return [("living_room.png", cam_loc, rot, 70.0), ("living_room_table.png", close_loc, close_rot, 45.0),
            ("living_room_cthulhu.png", cth_loc, cth_rot, 45.0)]


def capture_later(shots, seconds_to_wait=float(os.environ.get("ROOM_SETTLE_SECONDS", "90"))):
    # Capturing straight after import shows placeholder materials and unsettled lighting. Let the editor
    # tick until the imported materials' shaders have compiled and Lumen and auto exposure settle.
    state = {"elapsed": 0.0, "handle": None}
    caps = []
    for filename, loc, rot, fov in shots:
        cap = actors.spawn_actor_from_class(unreal.SceneCapture2D, loc, rot)
        rt = unreal.RenderingLibrary.create_render_target2d(world(), 1600, 900, unreal.TextureRenderTargetFormat.RTF_RGBA8)
        cc = cap.capture_component2d
        cc.set_editor_property("texture_target", rt)
        cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        cc.set_editor_property("capture_every_frame", True)  # every frame, so temporal effects converge
        cc.set_editor_property("always_persist_rendering_state", True)  # keeps Lumen and exposure history between frames
        cc.set_editor_property("fov_angle", fov)
        caps.append((filename, cap, rt))

    def tick(delta):
        state["elapsed"] += delta
        if state["elapsed"] < seconds_to_wait:
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
