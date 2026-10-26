#!/usr/bin/env python3
"""Prepare a throwaway copy of the repo (ci-work/) that tests the checked-out component.

The shipped configs pull the component and the shared files from GitHub `main`; CI must test the
commit under test instead. In ci-work/:
  * common/base.yaml points external_components at the local components/ directory (ESPHome appends
    external_components lists from packages, so an overlay would load `klink` twice: rewrite instead);
  * every examples/*.yaml with a `remote_package` is rewritten to local `!include`s of the same files
    (with their vars) and moved to the root of the copy, because !include may not leave the config
    directory; examples without a remote_package (they already use local includes) are copied
    unchanged and stay in examples/;
  * a dummy secrets.yaml covers every !secret used (nothing is flashed from CI).
Run from the repository root:  python3 tools/ci_prepare.py
"""

import base64
import pathlib
import re
import shutil
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORK = ROOT / "ci-work"


def fail(message):
    raise SystemExit("ci_prepare: " + message)


def patch_base():
    components = (WORK / "components").resolve()
    base = WORK / "common" / "base.yaml"
    override = (
        "# CI override (tools/ci_prepare.py): use the checked-out component.\n"
        "external_components:\n"
        "  - source:\n"
        "      type: local\n"
        f"      path: {components}\n"
        "    components: [klink]\n"
    )
    text, count = re.subn(
        r"(?m)^external_components:[ \t]*\n(?:[ \t]+\S.*\n)*", override, base.read_text()
    )
    if count != 1:
        fail("expected exactly one external_components block in common/base.yaml")
    base.write_text(text)


def local_packages(block):
    """Turn the text of a `packages:` block holding a remote_package into local includes."""
    data = yaml.safe_load(block)
    remote = data["packages"]["remote_package"]
    lines = ["packages:"]
    for i, entry in enumerate(remote["files"]):
        if isinstance(entry, str):
            lines.append(f"  p{i}: !include {entry}")
        else:
            vars_ = ", ".join(f'{k}: "{v}"' for k, v in (entry.get("vars") or {}).items())
            lines.append(f"  p{i}: !include {{file: {entry['path']}, vars: {{{vars_}}}}}")
    return "\n".join(lines) + "\n"


def rewrite_examples():
    for example in sorted((WORK / "examples").glob("*.yaml")):
        text = example.read_text()
        match = re.search(r"(?m)^packages:[ \t]*\n(?:[ \t]+\S.*\n|[ \t]*\n)*", text)
        if match is None or "remote_package" not in match.group(0):
            # Already uses local !include paths (or no packages at all): copied unchanged.
            continue
        text = text[: match.start()] + local_packages(match.group(0)) + text[match.end():]
        (WORK / example.name).write_text(text)
        example.unlink()


def write_secrets():
    names = set()
    for path in WORK.rglob("*.yaml"):
        names.update(re.findall(r"!secret\s+([A-Za-z0-9_]+)", path.read_text()))
    lines = []
    for name in sorted(names):
        value = (
            base64.b64encode(b"0123456789abcdef0123456789abcdef").decode()
            if "key" in name
            else "ci-dummy-value"
        )
        lines.append(f'{name}: "{value}"')
    (WORK / "secrets.yaml").write_text("".join(line + "\n" for line in lines))
    print(f"ci_prepare: prepared ci-work/ with {len(names)} dummy secret(s)")


def main():
    if WORK.exists():
        shutil.rmtree(WORK)
    WORK.mkdir()
    for name in ("boards", "common", "components", "examples"):
        shutil.copytree(ROOT / name, WORK / name, ignore=shutil.ignore_patterns("__pycache__"))
    for path in ROOT.glob("kessil-tuna-*.yaml"):
        shutil.copy(path, WORK / path.name)
    patch_base()
    rewrite_examples()
    write_secrets()


if __name__ == "__main__":
    sys.exit(main())
