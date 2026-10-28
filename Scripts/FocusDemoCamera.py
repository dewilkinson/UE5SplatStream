# FocusDemoCamera.py
# Moves the editor viewport to the level's "DemoCamera" actor, or else to a SoundStage's start view (see OpenDemo.ps1
# -Map). Run it from the editor's Python console too: py <path>/FocusDemoCamera.py

import unreal

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
camera = next((a for a in actors if a.get_actor_label() == "DemoCamera"), None)
stage = next((a for a in actors if isinstance(a, unreal.SoundStageActor)), None)
if camera:
    editor.set_level_viewport_camera_info(camera.get_actor_location(), camera.get_actor_rotation())
    unreal.log("[OpenDemo] viewport moved to DemoCamera")
elif stage:
    start = stage.get_start_transform()
    editor.set_level_viewport_camera_info(start.translation, start.rotation.rotator())
    unreal.log("[OpenDemo] viewport moved to %s's start" % stage.get_actor_label())
