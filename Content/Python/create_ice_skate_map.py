"""Creates the isolated skating test map /Game/Prototype/IceSkate/L_IceSkate.

The map is empty on purpose: ASkateGameMode (set as the map's GameMode Override) spawns the
rink, ball, cones, goal and lighting at runtime, so no other assets are needed.

Run inside the Unreal Editor (after the C++ module is compiled):
  Tools > Execute Python Script... > pick this file
or in the Output Log (Cmd: Python):
  py "Content/Python/create_ice_skate_map.py"
"""
import unreal

MAP_PATH = "/Game/Prototype/IceSkate/L_IceSkate"
GAME_MODE = "/Script/IceFootball.SkateGameMode"


def main():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    if assets.does_asset_exist(MAP_PATH):
        unreal.log_warning(f"{MAP_PATH} already exists - opening it instead.")
        levels.load_level(MAP_PATH)
        return

    if not levels.new_level(MAP_PATH):
        raise RuntimeError(f"Could not create {MAP_PATH}")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings_list = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)
    if not settings_list:
        raise RuntimeError("No WorldSettings in the new level")
    game_mode = unreal.load_class(None, GAME_MODE)
    if game_mode is None:
        raise RuntimeError(f"{GAME_MODE} not found - compile the IceFootball module first")
    settings_list[0].set_editor_property("default_game_mode", game_mode)

    levels.save_current_level()
    unreal.log(f"Created {MAP_PATH} with GameMode Override = SkateGameMode. Press Play.")


main()
