from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPO_ROOT / "scripts" / "generate_targets.py"
SPEC = importlib.util.spec_from_file_location("generate_targets", MODULE_PATH)
generate_targets = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(generate_targets)


def valid_target() -> dict:
    return {
        "id": "example",
        "displayName": "Example",
        "description": "Example target",
        "icon": "image-x-generic",
        "pluginTypes": ["ShareUrl", "Export"],
        "constraints": ["mimeType:image/*"],
        "request": {
            "url": "https://example.test/upload",
            "method": "POST",
            "multipart": {
                "fields": {"token": "abc"},
                "fileField": "file",
            },
        },
        "response": {
            "type": "json_pointer",
            "pointer": "/data/url",
        },
    }


def test_validate_target_accepts_stock_targets_file() -> None:
    targets = generate_targets.load_targets(REPO_ROOT / "targets.json")
    assert len(targets) >= 2
    for target in targets:
        generate_targets.validate_target(target)


def test_generate_target_writes_expected_files(tmp_path: Path) -> None:
    target = valid_target()

    target_id = generate_targets.generate_target(target, tmp_path, "1.2.3")

    assert target_id == "example"
    metadata = json.loads((tmp_path / "exampleplugin.json").read_text(encoding="utf-8"))
    assert metadata["KPlugin"]["Name"] == "Example"
    assert metadata["KPlugin"]["Version"] == "1.2.3"
    cpp = (tmp_path / "exampleplugin.cpp").read_text(encoding="utf-8")
    assert 'K_PLUGIN_CLASS_WITH_JSON(ExamplePlugin, "exampleplugin.json")' in cpp
    assert '"preUpload"' not in cpp


def test_generate_targets_and_write_targets_cmake(tmp_path: Path) -> None:
    targets = [valid_target(), valid_target() | {"id": "example2"}]

    ids = generate_targets.generate_targets(targets, tmp_path, "0.1.0")
    generate_targets.write_targets_cmake(ids, tmp_path)

    assert ids == ["example", "example2"]
    assert (tmp_path / "targets.cmake").read_text(encoding="utf-8") == (
        'set(IMSHARE_TARGET_IDS "example;example2")\n'
    )


def test_load_targets_rejects_empty_file(tmp_path: Path) -> None:
    path = tmp_path / "targets.json"
    path.write_text('{"targets":[]}', encoding="utf-8")

    with pytest.raises(ValueError, match="targets.json has no targets"):
        generate_targets.load_targets(path)


@pytest.mark.parametrize(
    ("mutator", "message"),
    [
        (lambda target: target | {"id": "Invalid"}, "Invalid target id"),
        (
            lambda target: target | {"request": {"url": "", "method": "POST", "multipart": {"fileField": "file"}}},
            "request.url must be a non-empty string",
        ),
        (
            lambda target: target | {"request": target["request"] | {"type": "weird"}},
            "request.type must be multipart or raw",
        ),
        (
            lambda target: target | {"response": {"type": "unknown"}},
            "response.type must be text_url, regex, or json_pointer",
        ),
        (
            lambda target: target | {"pluginTypes": "ShareUrl"},
            "pluginTypes must be a list",
        ),
        (
            lambda target: target | {"constraints": "mimeType:image/*"},
            "constraints must be a list",
        ),
    ],
)
def test_validate_target_rejects_invalid_core_fields(mutator, message: str) -> None:
    target = mutator(valid_target())
    with pytest.raises(ValueError, match=message):
        generate_targets.validate_target(target)


@pytest.mark.parametrize(
    ("rule", "message"),
    [
        (
            {"mime": ["image"], "fileHandling": "inplace_copy", "commands": [{"argv": ["tool", "${FILE}"]}]},
            r"mime entries must be exact MIME types, type/\*, or \*/\*",
        ),
        (
            {"mime": ["image/*"], "fileHandling": "weird", "commands": [{"argv": ["tool", "${FILE}"]}]},
            "fileHandling must be inplace_copy or output_file",
        ),
        (
            {"mime": ["image/*"], "fileHandling": "inplace_copy", "commands": []},
            r"commands must be a non-empty list",
        ),
        (
            {"mime": ["image/*"], "fileHandling": "inplace_copy", "commands": [{"argv": ["tool"]}]},
            r"argv must include \${FILE}",
        ),
        (
            {"mime": ["image/*"], "fileHandling": "inplace_copy", "commands": [{"argv": ["tool", "${FILE}", "${OUT_FILE}"]}]},
            r"must not include \${OUT_FILE} for inplace_copy",
        ),
        (
            {"mime": ["image/*"], "fileHandling": "output_file", "commands": [{"argv": ["tool", "${FILE}"]}]},
            r"must include \${OUT_FILE}",
        ),
        (
            {"mime": ["image/*"], "fileHandling": "output_file", "commands": [{"argv": ["tool", "${FILE}", "${OUT_FILE}"]}, {"argv": ["tool", "${FILE}", "${OUT_FILE}"]}]},
            "must contain exactly one command for output_file",
        ),
        (
            {"mime": ["image/*"], "fileHandling": "inplace_copy", "commands": [{"argv": ["tool", "${FILE}", "${FILENAME}"]}]},
            r"contains unsupported placeholder \${FILENAME}",
        ),
        (
            {"mime": ["image/*"], "fileHandling": "inplace_copy", "commands": [{"argv": ["tool", "${FILE}"]}], "timeoutMs": 0},
            "timeoutMs must be a positive integer",
        ),
    ],
)
def test_validate_target_rejects_invalid_preupload_rules(rule: dict, message: str) -> None:
    target = valid_target() | {"preUpload": [rule]}
    with pytest.raises(ValueError, match=message):
        generate_targets.validate_target(target)


def test_validate_target_accepts_valid_preupload_rules() -> None:
    target = valid_target() | {
        "preUpload": [
            {
                "mime": ["image/*"],
                "fileHandling": "inplace_copy",
                "commands": [
                    {"argv": ["tool", "${FILE}"]},
                    {"argv": ["tool2", "${FILE}"]},
                ],
            },
            {
                "mime": ["*/*"],
                "fileHandling": "output_file",
                "commands": [
                    {"argv": ["tool", "${FILE}", "${OUT_FILE}"]},
                ],
                "timeoutMs": 1,
            },
        ]
    }

    generate_targets.validate_target(target)
