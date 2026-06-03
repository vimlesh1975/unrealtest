import unreal


ASSET_DIR = "/Game/Materials"
ASSET_NAME = "M_LiveChromaKey"
ASSET_PATH = f"{ASSET_DIR}/{ASSET_NAME}"
DEFAULT_MEDIA_TEXTURE_NAME = "T_DefaultChromaKeyMedia"
DEFAULT_MEDIA_TEXTURE_PATH = f"{ASSET_DIR}/{DEFAULT_MEDIA_TEXTURE_NAME}"


def make_scalar(material, name, default_value, x, y):
    expr = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionScalarParameter,
        x,
        y,
    )
    expr.set_editor_property("parameter_name", name)
    expr.set_editor_property("default_value", default_value)
    return expr


def main():
    unreal.EditorAssetLibrary.make_directory(ASSET_DIR)

    default_media_texture = unreal.EditorAssetLibrary.load_asset(DEFAULT_MEDIA_TEXTURE_PATH)
    if not default_media_texture:
        media_texture_factory_class = getattr(unreal, "MediaTextureFactoryNew", None)
        default_media_texture = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            DEFAULT_MEDIA_TEXTURE_NAME,
            ASSET_DIR,
            unreal.MediaTexture,
            media_texture_factory_class() if media_texture_factory_class else None,
        )
    if default_media_texture:
        default_media_texture.set_editor_property("new_style_output", False)
        default_media_texture.set_editor_property("enable_gen_mips", False)
        default_media_texture.update_resource()
        unreal.EditorAssetLibrary.save_loaded_asset(default_media_texture)

    material = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ASSET_NAME,
            ASSET_DIR,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )

    if not material:
        raise RuntimeError(f"Could not create {ASSET_PATH}")

    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    texture = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionTextureSampleParameter2D,
        -700,
        -160,
    )
    texture.set_editor_property("parameter_name", "MediaTexture")
    if default_media_texture:
        texture.set_editor_property("texture", default_media_texture)
    texture.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_EXTERNAL)

    key_enabled = make_scalar(material, "KeyEnabled", 0.0, -700, 40)
    tolerance = make_scalar(material, "Tolerance", 0.12, -700, 180)
    softness = make_scalar(material, "Softness", 0.22, -700, 320)
    despill = make_scalar(material, "Despill", 0.75, -700, 460)

    custom = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionCustom,
        -180,
        -60,
    )
    custom.set_editor_property("description", "Live chroma key")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property(
        "code",
        "\n".join(
            [
                "float4 c = Color;",
                "float enabled = saturate(KeyEnabled);",
                "float greenDominance = c.g - max(c.r, c.b);",
                "float safeSoftness = max(Softness, 0.001);",
                "float keyAmount = smoothstep(Tolerance, Tolerance + safeSoftness, greenDominance);",
                "float spillAmount = saturate(greenDominance * 3.0) * saturate(Despill) * (1.0 - keyAmount * 0.65);",
                "c.g = lerp(c.g, max(c.r, c.b), spillAmount * enabled);",
                "c.a *= lerp(1.0, 1.0 - keyAmount, enabled);",
                "return c;",
            ]
        ),
    )

    inputs = []
    for name in ["Color", "KeyEnabled", "Tolerance", "Softness", "Despill"]:
        input_slot = unreal.CustomInput()
        input_slot.set_editor_property("input_name", name)
        inputs.append(input_slot)
    custom.set_editor_property("inputs", inputs)

    rgb_mask = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionComponentMask,
        180,
        -100,
    )
    rgb_mask.set_editor_property("r", True)
    rgb_mask.set_editor_property("g", True)
    rgb_mask.set_editor_property("b", True)
    rgb_mask.set_editor_property("a", False)

    alpha_mask = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionComponentMask,
        180,
        100,
    )
    alpha_mask.set_editor_property("r", False)
    alpha_mask.set_editor_property("g", False)
    alpha_mask.set_editor_property("b", False)
    alpha_mask.set_editor_property("a", True)

    connections = [
        unreal.MaterialEditingLibrary.connect_material_expressions(texture, "RGBA", custom, "Color"),
        unreal.MaterialEditingLibrary.connect_material_expressions(key_enabled, "", custom, "KeyEnabled"),
        unreal.MaterialEditingLibrary.connect_material_expressions(tolerance, "", custom, "Tolerance"),
        unreal.MaterialEditingLibrary.connect_material_expressions(softness, "", custom, "Softness"),
        unreal.MaterialEditingLibrary.connect_material_expressions(despill, "", custom, "Despill"),
        unreal.MaterialEditingLibrary.connect_material_expressions(custom, "", rgb_mask, ""),
        unreal.MaterialEditingLibrary.connect_material_expressions(custom, "", alpha_mask, ""),
        unreal.MaterialEditingLibrary.connect_material_property(rgb_mask, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
        unreal.MaterialEditingLibrary.connect_material_property(alpha_mask, "", unreal.MaterialProperty.MP_OPACITY),
    ]

    print(f"Created {ASSET_PATH}; connections={connections}")
    print("Custom inputs:", unreal.MaterialEditingLibrary.get_material_expression_input_names(custom))

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)


if __name__ == "__main__":
    main()
