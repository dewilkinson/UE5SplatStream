# FocusDemoCamera.py
# Moves the editor viewport to the level's "DemoCamera" actor, if it has one (see OpenDemo.ps1 -Map).

import unreal

for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if actor.get_actor_label() == "DemoCamera":
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
            actor.get_actor_location(), actor.get_actor_rotation())
        unreal.log("[OpenDemo] viewport moved to DemoCamera")
        break
