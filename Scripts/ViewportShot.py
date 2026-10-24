# ViewportShot.py
# Runs in the interactive editor (see ViewportShot.ps1). Takes HighResShots of the level viewport from
# the level's DemoCamera and from straight above (to check which way everything faces), letting the
# viewport render in real time first so Lumen and auto exposure settle. A viewport shot is the main
# view, so the full renderer runs, unlike a SceneCapture. Quits when done.

import os

import unreal

FIRST_WAIT = float(os.environ.get("VIEWPORT_SHOT_SECONDS", "120"))
NEXT_WAIT = 30.0
RES = os.environ.get("VIEWPORT_SHOT_RES", "1600x900")

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

poses = []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if actor.get_actor_label() == "DemoCamera":
        poses.append(("DemoCamera", actor.get_actor_location(), actor.get_actor_rotation()))
poses.append(("TopDown", unreal.Vector(30, 0, 270), unreal.Rotator(pitch=-89.9, yaw=0)))

level_editor.editor_set_game_view(True)  # hide editor icons and gizmos
try:
    level_editor.editor_set_viewport_realtime(True)  # Unreal turns realtime off in Remote Desktop sessions
except Exception as e:
    unreal.log_warning("[ViewportShot] could not force realtime: %s" % e)

state = {"elapsed": 0.0, "index": 0, "phase": "wait", "handle": None}


def set_pose(i):
    name, loc, rot = poses[i]
    editor.set_level_viewport_camera_info(loc, rot)
    unreal.log("[ViewportShot] viewing from %s" % name)


def tick(delta):
    state["elapsed"] += delta
    wait = FIRST_WAIT if state["index"] == 0 else NEXT_WAIT
    if state["phase"] == "wait" and state["elapsed"] >= wait:
        unreal.SystemLibrary.execute_console_command(editor.get_editor_world(), "HighResShot " + RES)
        unreal.log("[ViewportShot] HighResShot requested for %s" % poses[state["index"]][0])
        state["phase"], state["elapsed"] = "shot", 0.0
    elif state["phase"] == "shot" and state["elapsed"] >= 8.0:
        state["index"] += 1
        if state["index"] >= len(poses):
            unreal.unregister_slate_post_tick_callback(state["handle"])
            unreal.log("[ViewportShot] done")
            unreal.SystemLibrary.quit_editor()
            return
        set_pose(state["index"])
        state["phase"], state["elapsed"] = "wait", 0.0


set_pose(0)
state["handle"] = unreal.register_slate_post_tick_callback(tick)
unreal.log("[ViewportShot] %d views; waiting %.0f s before the first shot" % (len(poses), FIRST_WAIT))
