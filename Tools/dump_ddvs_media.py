import unreal

OUT_FILE = r"C:\Users\wtvision\Desktop\dd\unrealtest\Tools\ddvs_media_dump.txt"

ASSET_PATHS = [
    "/Game/000_wTV_AR/Media/MediaProfiles/ZX.ZX",
    "/Game/000_wTV_AR/Media/MediaProfiles/CATB.CATB",
    "/Game/000_wTV_AR/Media/MediaProfiles/CATC.CATC",
    "/Game/000_wTV_AR/Media/MediaProfiles/CATH.CATH",
    "/Game/000_wTV_AR/Media/Proxies/MediaOutput-01.MediaOutput-01",
    "/Game/000_wTV_AR/Media/Proxies/MediaOutput-02.MediaOutput-02",
    "/Game/000_wTV_AR/Media/Proxies/MediaOutput-03.MediaOutput-03",
    "/Game/000_wTV_AR/Media/Proxies/MediaOutput-04.MediaOutput-04",
    "/Game/MoSys/Media/Output/Output.Output",
]


def text(value, depth=0):
    if value is None:
        return "None"
    if depth > 3:
        return "..."
    if isinstance(value, (str, int, float, bool)):
        return str(value)
    if isinstance(value, unreal.Name):
        return str(value)
    if isinstance(value, unreal.Text):
        return str(value)
    if isinstance(value, unreal.Vector2D):
        return "({0}, {1})".format(value.x, value.y)
    if isinstance(value, (list, tuple)):
        return "[" + ", ".join(text(item, depth + 1) for item in value) + "]"
    if isinstance(value, unreal.Object):
        try:
            return "{0} {1}".format(value.get_class().get_name(), value.get_path_name())
        except Exception:
            return str(value)
    return str(value)


def reflected_properties(obj):
    names = []
    for name in dir(obj):
        if name.startswith("_"):
            continue
        if name in ("get_class", "get_name", "get_outer", "get_path_name", "get_world"):
            continue
        try:
            value = obj.get_editor_property(name)
        except Exception:
            continue
        names.append((name, value))
    return sorted(names, key=lambda item: item[0])


def dump_object(lines, obj, label):
    if not obj:
        lines.append("{0}: NOT FOUND".format(label))
        return

    lines.append("{0}: {1} {2}".format(label, obj.get_class().get_name(), obj.get_path_name()))
    for name, value in reflected_properties(obj):
        lines.append("  {0}: {1}".format(name, text(value)))

    for name in (
        "media_sources",
        "media_outputs",
        "override_timecode_provider",
        "timecode_provider",
        "override_custom_time_step",
        "custom_time_step",
        "proxy",
        "dynamic_proxy",
        "output_configuration",
        "number_of_blackmagic_buffers",
        "interlaced_fields_timecode_need_to_match",
        "log_drop_frame",
        "invert_key_output",
        "hdr_options",
    ):
        try:
            lines.append("  {0}: {1}".format(name, text(obj.get_editor_property(name))))
        except Exception:
            pass

    for method_name in (
        "num_media_outputs",
        "num_media_sources",
        "get_media_output",
        "get_media_source",
        "get_media_output",
        "get_leaf_media_output",
    ):
        method = getattr(obj, method_name, None)
        if not callable(method):
            continue
        try:
            if method_name.startswith("num_"):
                lines.append("  {0}(): {1}".format(method_name, method()))
            elif method_name in ("get_media_output", "get_media_source"):
                for index in range(8):
                    try:
                        value = method(index)
                    except Exception:
                        break
                    lines.append("  {0}({1}): {2}".format(method_name, index, text(value)))
            else:
                lines.append("  {0}(): {1}".format(method_name, text(method())))
        except Exception as exc:
            lines.append("  {0}(): ERROR {1}".format(method_name, exc))


def main():
    lines = []
    for path in ASSET_PATHS:
        asset = unreal.EditorAssetLibrary.load_asset(path)
        dump_object(lines, asset, path)
        lines.append("")

    with open(OUT_FILE, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))
    unreal.log("Wrote DDVS media dump to {0}".format(OUT_FILE))


main()
