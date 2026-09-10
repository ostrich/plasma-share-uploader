"""Check packaged definitions and a shared corpus against JSON Schema and C++."""
import json
from pathlib import Path
import subprocess
import sys

try:
    from jsonschema import Draft202012Validator
except ImportError:
    sys.exit("Install tests/requirements.txt into the Python environment selected by CMake's Python3_EXECUTABLE.")

root = Path(__file__).resolve().parent.parent
schema = json.loads((root / "schemas/target-v1.schema.json").read_text())
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
cases = json.loads((root / "tests/fixtures/target-schema-cases.json").read_text())
for file in sorted((root / "targets").rglob("*.json")):
    cases.append({"name": str(file.relative_to(root)), "target": json.loads(file.read_text()), "valid": True})

errors = []
for case in cases:
    schema_errors = list(validator.iter_errors(case["target"]))
    schema_valid = not schema_errors
    result = subprocess.run([sys.argv[1]], input=json.dumps(case["target"]), capture_output=True, text=True)
    if result.returncode not in (0, 1):
        errors.append(f'{case["name"]}: validator crashed: {result.stderr}')
    if schema_valid != case.get("schemaValid", case["valid"]):
        errors.append(f'{case["name"]}: unexpected schema result: {schema_errors}')
    if (result.returncode == 0) != case["valid"]:
        errors.append(f'{case["name"]}: unexpected C++ result: {result.stderr}')
if errors:
    sys.exit("\n".join(errors))
print(f"Validated {len(cases)} definitions against JSON Schema and the application parser")
