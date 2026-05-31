import unreal


LEVEL_PATH = "/Game/Maps/FourCameraLevel"


def spawn_mesh(mesh_path, label, location, scale):
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor,
        unreal.Vector(*location),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(mesh)
    actor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


def set_movable(component):
    if not component:
        return
    try:
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
    except Exception:
        component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)


def make_movable(actor):
    for property_name in (
        "root_component",
        "light_component",
        "directional_light_component",
        "sky_light_component",
        "camera_component",
    ):
        try:
            set_movable(actor.get_editor_property(property_name))
        except Exception:
            pass
    return actor


def spawn_camera(label, location, rotation):
    camera = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.CameraActor,
        unreal.Vector(*location),
        unreal.Rotator(*rotation),
    )
    camera.set_actor_label(label)
    return make_movable(camera)


unreal.EditorAssetLibrary.make_directory("/Game/Maps")
if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
    unreal.EditorAssetLibrary.delete_asset(LEVEL_PATH)

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.new_level(LEVEL_PATH)

spawn_mesh("/Engine/BasicShapes/Cube.Cube", "Demo_Cube", (-240.0, 0.0, 50.0), (1.0, 1.0, 1.0))
spawn_mesh("/Engine/BasicShapes/Sphere.Sphere", "Demo_Sphere", (0.0, 0.0, 50.0), (1.0, 1.0, 1.0))
spawn_mesh("/Engine/BasicShapes/Cone.Cone", "Demo_Cone", (240.0, 0.0, 50.0), (1.0, 1.0, 1.0))

floor = spawn_mesh("/Engine/BasicShapes/Cube.Cube", "Demo_Floor", (0.0, 0.0, -5.0), (8.0, 5.0, 0.1))

light = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.DirectionalLight,
    unreal.Vector(0.0, 0.0, 400.0),
    unreal.Rotator(-45.0, -30.0, 0.0),
)
light.set_actor_label("Demo_KeyLight")
make_movable(light)

sky_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkyLight,
    unreal.Vector(0.0, 0.0, 300.0),
    unreal.Rotator(0.0, 0.0, 0.0),
)
sky_light.set_actor_label("Demo_SkyLight")
make_movable(sky_light)

spawn_camera("Camera_01_Front", (0.0, -720.0, 260.0), (-18.0, 90.0, 0.0))
spawn_camera("Camera_02_Left", (-720.0, 0.0, 260.0), (-18.0, 0.0, 0.0))
spawn_camera("Camera_03_Right", (720.0, 0.0, 260.0), (-18.0, 180.0, 0.0))
spawn_camera("Camera_04_Top", (0.0, 0.0, 980.0), (-90.0, 0.0, 0.0))

world = unreal.EditorLevelLibrary.get_editor_world()
world_settings = world.get_world_settings()
game_mode_class = unreal.load_class(None, "/Script/GGGProject.GGGGameModeBase")
if game_mode_class:
    world_settings.set_editor_property("default_game_mode", game_mode_class)

unreal.EditorAssetLibrary.save_asset(LEVEL_PATH)
unreal.log("Created and saved /Game/Maps/FourCameraLevel with four cameras.")
