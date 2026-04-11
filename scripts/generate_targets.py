#!/usr/bin/env python3
"""Generate plugin sources and metadata from targets.json.

Each target entry emits a plugin JSON file plus a C++ wrapper, and writes
targets.cmake with the generated target IDs for the CMake build.
"""
import argparse
import json
import os
import re
from pathlib import Path

ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
MIME_RE = re.compile(r"^[^/\s]+/[^/\s]+$")
MIME_WILDCARD_RE = re.compile(r"^[^/\s]+/\*$")
PLACEHOLDER_RE = re.compile(r"\$\{([A-Z_]+)\}")

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


def validate_target_id(target: dict) -> str:
    target_id = target.get("id")
    if not target_id or not ID_RE.match(target_id):
        raise ValueError(f"Invalid target id: {target_id!r}")
    return target_id


def validate_string_map(target_id: str, value, path: str) -> None:
    if value is not None and not isinstance(value, dict):
        raise ValueError(f"Target {target_id!r} {path} must be an object")
    if isinstance(value, dict):
        for name, entry in value.items():
            if not isinstance(name, str) or not name:
                raise ValueError(f"Target {target_id!r} {path} keys must be non-empty strings")
            if not isinstance(entry, str):
                raise ValueError(f"Target {target_id!r} {path} values must be strings")


def validate_request(target_id: str, request) -> None:
    if not isinstance(request, dict):
        raise ValueError(f"Target {target_id!r} missing request object")

    url = request.get("url")
    method = request.get("method")
    request_type = request.get("type", "multipart")
    if not isinstance(url, str) or not url:
        raise ValueError(f"Target {target_id!r} request.url must be a non-empty string")
    if not isinstance(request_type, str) or request_type not in {"multipart", "raw"}:
        raise ValueError(f"Target {target_id!r} request.type must be multipart or raw")
    if not isinstance(method, str) or not method:
        raise ValueError(f"Target {target_id!r} request.method must be a non-empty string")

    validate_string_map(target_id, request.get("headers"), "request.headers")

    if request_type == "multipart":
        if method.upper() != "POST":
            raise ValueError(f"Target {target_id!r} request.method must be POST for multipart")
        multipart = request.get("multipart")
        if not isinstance(multipart, dict):
            raise ValueError(f"Target {target_id!r} request.multipart must be an object")

        file_field = multipart.get("fileField")
        if not isinstance(file_field, str) or not file_field:
            raise ValueError(f"Target {target_id!r} request.multipart.fileField must be a non-empty string")

        validate_string_map(target_id, multipart.get("fields", {}), "request.multipart.fields")
    else:
        if method.upper() not in {"POST", "PUT"}:
            raise ValueError(f"Target {target_id!r} request.method must be POST or PUT for raw")

def validate_mime_pattern(rule_path: str, pattern) -> None:
    if not isinstance(pattern, str) or not pattern:
        raise ValueError(f"{rule_path}.mime must contain non-empty strings")
    if pattern != "*/*" and not MIME_RE.match(pattern) and not MIME_WILDCARD_RE.match(pattern):
        raise ValueError(f"{rule_path}.mime entries must be exact MIME types, type/*, or */*")
    if pattern.startswith("*/") and pattern != "*/*":
        raise ValueError(f"{rule_path}.mime entries must be exact MIME types, type/*, or */*")


def validate_pre_upload_command(command_path: str, command, file_handling: str) -> bool:
    if not isinstance(command, dict):
        raise ValueError(f"{command_path} must be an object")

    argv = command.get("argv")
    if not isinstance(argv, list) or not argv:
        raise ValueError(f"{command_path}.argv must be a non-empty list")

    has_file = False
    has_out_file = False
    for arg in argv:
        if not isinstance(arg, str) or not arg:
            raise ValueError(f"{command_path}.argv must contain non-empty strings")
        has_file = has_file or "${FILE}" in arg
        has_out_file = has_out_file or "${OUT_FILE}" in arg
        for placeholder in PLACEHOLDER_RE.findall(arg):
            if placeholder not in {"FILE", "OUT_FILE"}:
                raise ValueError(
                    f"{command_path}.argv contains unsupported placeholder ${{{placeholder}}}"
                )

    if not has_file:
        raise ValueError(f"{command_path}.argv must include ${{FILE}}")

    if file_handling == "inplace_copy" and has_out_file:
        raise ValueError(f"{command_path}.argv must not include ${{OUT_FILE}} for inplace_copy")

    return has_out_file


def validate_pre_upload(target_id: str, pre_upload) -> None:
    if pre_upload is not None and not isinstance(pre_upload, list):
        raise ValueError(f"Target {target_id!r} preUpload must be a list")
    if isinstance(pre_upload, list):
        for index, rule in enumerate(pre_upload):
            rule_path = f"Target {target_id!r} preUpload[{index}]"
            if not isinstance(rule, dict):
                raise ValueError(f"{rule_path} must be an object")

            mime_patterns = rule.get("mime")
            if not isinstance(mime_patterns, list) or not mime_patterns:
                raise ValueError(f"{rule_path}.mime must be a non-empty list")
            for pattern in mime_patterns:
                validate_mime_pattern(rule_path, pattern)

            file_handling = rule.get("fileHandling")
            if file_handling not in {"inplace_copy", "output_file"}:
                raise ValueError(f"{rule_path}.fileHandling must be inplace_copy or output_file")

            commands = rule.get("commands")
            if not isinstance(commands, list) or not commands:
                raise ValueError(f"{rule_path}.commands must be a non-empty list")
            if file_handling == "output_file" and len(commands) != 1:
                raise ValueError(f"{rule_path}.commands must contain exactly one command for output_file")

            saw_out_file = False
            for command_index, command in enumerate(commands):
                command_path = f"{rule_path}.commands[{command_index}]"
                saw_out_file = saw_out_file or validate_pre_upload_command(
                    command_path, command, file_handling
                )

            if file_handling == "output_file" and not saw_out_file:
                raise ValueError(f"{rule_path}.commands[0].argv must include ${{OUT_FILE}}")

            timeout_ms = rule.get("timeoutMs")
            if timeout_ms is not None and (not isinstance(timeout_ms, int) or timeout_ms <= 0):
                raise ValueError(f"{rule_path}.timeoutMs must be a positive integer")


def validate_response(target_id: str, response) -> None:
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
        if not pointer.startswith("/"):
            raise ValueError(f"Target {target_id!r} response.pointer must start with '/'")


def validate_optional_lists(target_id: str, target: dict) -> None:
    plugin_types = target.get("pluginTypes", ["ShareUrl"])
    if plugin_types is not None and not isinstance(plugin_types, list):
        raise ValueError(f"Target {target_id!r} pluginTypes must be a list")

    constraints = target.get("constraints", [])
    if constraints is not None and not isinstance(constraints, list):
        raise ValueError(f"Target {target_id!r} constraints must be a list")


def validate_target(target: dict) -> None:
    target_id = validate_target_id(target)
    validate_request(target_id, target.get("request"))
    validate_pre_upload(target_id, target.get("preUpload"))
    validate_response(target_id, target.get("response"))
    validate_optional_lists(target_id, target)


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


def load_targets(path: Path) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    targets = data.get("targets", [])
    if not targets:
        raise ValueError("targets.json has no targets")
    return targets


def generate_targets(targets: list[dict], out_dir: Path, version: str) -> list[str]:
    ids = []
    for target in targets:
        ids.append(generate_target(target, out_dir, version))
    return ids


def write_targets_cmake(ids: list[str], out_dir: Path) -> None:
    cmake_path = out_dir / "targets.cmake"
    ids_list = ";".join(ids)
    cmake_path.write_text(f"set(IMSHARE_TARGET_IDS \"{ids_list}\")\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    in_path = Path(args.input)
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    targets = load_targets(in_path)
    ids = generate_targets(targets, out_dir, args.version)
    write_targets_cmake(ids, out_dir)


if __name__ == "__main__":
    main()
