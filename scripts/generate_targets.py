#!/usr/bin/env python3
import argparse
import json
import os
import re
from pathlib import Path

ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]*$")

CPP_TEMPLATE = """
#include \"sharejob.h\"

#include <KPluginFactory>
#include <Purpose/PluginBase>

namespace {{
constexpr const char kTargetConfig[] = {config_literal};
}}

class {class_name} final : public Purpose::PluginBase
{{
    Q_OBJECT
public:
    explicit {class_name}(QObject *parent, const KPluginMetaData &metaData, const QVariantList &args)
        : Purpose::PluginBase(parent)
    {{
        Q_UNUSED(metaData)
        Q_UNUSED(args)
    }}

    Purpose::Job *createJob() const override
    {{
        return new ShareJob(QByteArray::fromRawData(kTargetConfig, sizeof(kTargetConfig) - 1));
    }}
}};

K_PLUGIN_CLASS_WITH_JSON({class_name}, "{json_filename}")

#include "{moc_filename}"
""".lstrip()


def camel_case(value: str) -> str:
    parts = re.split(r"[^a-zA-Z0-9]", value)
    return "".join(p[:1].upper() + p[1:] for p in parts if p)


def validate_target(target: dict) -> None:
    target_id = target.get("id")
    if not target_id or not ID_RE.match(target_id):
        raise ValueError(f"Invalid target id: {target_id!r}")

    request = target.get("request")
    if not isinstance(request, dict):
        raise ValueError(f"Target {target_id!r} missing request object")

    url = request.get("url")
    method = request.get("method")
    request_type = request.get("type", "multipart")
    if not isinstance(url, str) or not url:
        raise ValueError(f"Target {target_id!r} request.url must be a non-empty string")
    if not isinstance(request_type, str) or request_type not in {"multipart", "raw"}:
        raise ValueError(f"Target {target_id!r} request.type must be multipart or raw")
    if not isinstance(method, str):
        raise ValueError(f"Target {target_id!r} request.method must be a string")

    if request_type == "multipart":
        if method.upper() != "POST":
            raise ValueError(f"Target {target_id!r} request.method must be POST for multipart")
        multipart = request.get("multipart")
        if not isinstance(multipart, dict):
            raise ValueError(f"Target {target_id!r} request.multipart must be an object")

        file_field = multipart.get("fileField")
        if not isinstance(file_field, str) or not file_field:
            raise ValueError(f"Target {target_id!r} request.multipart.fileField must be a non-empty string")

        fields = multipart.get("fields", {})
        if not isinstance(fields, dict):
            raise ValueError(f"Target {target_id!r} request.multipart.fields must be an object")
    else:
        if method.upper() not in {"POST", "PUT"}:
            raise ValueError(f"Target {target_id!r} request.method must be POST or PUT for raw")

    response = target.get("response")
    if not isinstance(response, dict):
        raise ValueError(f"Target {target_id!r} missing response object")

    response_type = response.get("type")
    if response_type not in {"text_url", "regex", "json_pointer"}:
        raise ValueError(f"Target {target_id!r} response.type must be text_url, regex, or json_pointer")

    if response_type == "regex":
        pattern = response.get("pattern")
        if not isinstance(pattern, str) or not pattern:
            raise ValueError(f"Target {target_id!r} response.pattern must be a non-empty string")
        group = response.get("group", 1)
        if not isinstance(group, int) or group < 0:
            raise ValueError(f"Target {target_id!r} response.group must be a non-negative integer")

    if response_type == "json_pointer":
        pointer = response.get("pointer")
        if not isinstance(pointer, str) or not pointer:
            raise ValueError(f"Target {target_id!r} response.pointer must be a non-empty string")

    plugin_types = target.get("pluginTypes", ["ShareUrl"])
    if plugin_types is not None and not isinstance(plugin_types, list):
        raise ValueError(f"Target {target_id!r} pluginTypes must be a list")

    constraints = target.get("constraints", [])
    if constraints is not None and not isinstance(constraints, list):
        raise ValueError(f"Target {target_id!r} constraints must be a list")


def generate_target(target, out_dir: Path, version: str):
    validate_target(target)
    target_id = target.get("id")

    display_name = target.get("displayName", target_id)
    description = target.get("description", "")
    icon = target.get("icon", "image-x-generic")
    plugin_types = target.get("pluginTypes", ["ShareUrl"]) or ["ShareUrl"]
    constraints = target.get("constraints", [])

    meta = {
        "KPlugin": {
            "Name": display_name,
            "Description": description,
            "Icon": icon,
            "License": "GPL-3.0-or-later",
            "Version": version,
        },
        "X-Purpose-ActionDisplay": display_name,
        "X-Purpose-PluginTypes": plugin_types,
    }
    if constraints:
        meta["X-Purpose-Constraints"] = constraints

    json_filename = f"{target_id}plugin.json"
    cpp_filename = f"{target_id}plugin.cpp"

    json_path = out_dir / json_filename
    json_path.write_text(json.dumps(meta, indent=2), encoding="utf-8")

    class_name = f"{camel_case(target_id)}Plugin"
    config_json = json.dumps(target, ensure_ascii=True, separators=(",", ":"))
    config_literal = json.dumps(config_json)
    cpp = CPP_TEMPLATE.format(
        config_literal=config_literal,
        class_name=class_name,
        json_filename=json_filename,
        moc_filename=f"{target_id}plugin.moc",
    )
    (out_dir / cpp_filename).write_text(cpp, encoding="utf-8")

    return target_id


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    in_path = Path(args.input)
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    data = json.loads(in_path.read_text(encoding="utf-8"))
    targets = data.get("targets", [])
    if not targets:
        raise ValueError("targets.json has no targets")

    ids = []
    for target in targets:
        ids.append(generate_target(target, out_dir, args.version))

    cmake_path = out_dir / "targets.cmake"
    ids_list = ";".join(ids)
    cmake_path.write_text(f"set(IMSHARE_TARGET_IDS \"{ids_list}\")\n", encoding="utf-8")


if __name__ == "__main__":
    main()
