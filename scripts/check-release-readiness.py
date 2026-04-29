#!/usr/bin/env python3
"""
Release Readiness Scorecard for flux

Usage:
    python3 scripts/check-release-readiness.py
    python3 scripts/check-release-readiness.py --api-matrix

This script scans the codebase and produces a Markdown scorecard that maps
onto docs/dev/release-process.md.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PUBLIC_HEADER_DIR = PROJECT_ROOT / "include" / "flux"


def read_file(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return ""


def extract_public_api(header_text: str) -> List[Tuple[str, str]]:
    """
    Extract public API functions from header text.
    Returns list of (module_hint, function_name).
    """
    functions: List[Tuple[str, str]] = []
    current_module = "Misc"
    for line in header_text.splitlines():
        raw = line.strip()
        if raw.startswith("/*") and "---" in raw:
            continue
        if raw.startswith("#"):
            continue
        if 'FX_API' not in raw or '(' not in raw:
            continue
        if raw.lower().startswith("* ") and any(x in raw.lower() for x in [
            "context", "surface", "canvas", "transform", "paint",
            "image", "path", "font", "text", "draw", "matrix"
        ]):
            for candidate in [
                "Context", "Surface", "Canvas", "Transform", "Paint",
                "Image", "Path", "Font", "Text", "Drawing", "Matrix"
            ]:
                if candidate.lower() in raw.lower():
                    current_module = candidate
                    break
        code = raw.split('/*')[0].split('//')[0]
        if 'FX_API' not in code:
            continue
        before_paren = code.split('(')[0]
        words = before_paren.strip().split()
        if not words:
            continue
        fn = words[-1].lstrip('*')
        if fn in ('__attribute__', '__declspec', 'FX_API'):
            continue
        if not fn.startswith('fx_'):
            continue
        if fn.startswith("fx_context_"):
            mod = "Context"
        elif fn.startswith("fx_surface_"):
            mod = "Surface"
        elif fn in ("fx_clear", "fx_canvas_op_count"):
            mod = "Canvas"
        elif fn.startswith(("fx_save", "fx_restore", "fx_translate", "fx_scale", "fx_rotate", "fx_concat", "fx_set_matrix", "fx_get_matrix")):
            mod = "Transform"
        elif fn == "fx_paint_init":
            mod = "Paint"
        elif fn.startswith("fx_image_"):
            mod = "Image"
        elif fn.startswith("fx_path_"):
            mod = "Path"
        elif fn.startswith(("fx_font_", "fx_glyph_run_")):
            mod = "Font/Text"
        elif fn.startswith(("fx_fill_", "fx_stroke_", "fx_draw_")):
            mod = "Drawing"
        elif fn.startswith(("fx_matrix_", "fx_path_transform")):
            mod = "Matrix"
        elif fn.startswith(("fx_color_", "fx_rect_")):
            mod = "Inline"
        else:
            mod = current_module
        functions.append((mod, fn))
    return functions


def find_implementations(src_dir: Path) -> Set[str]:
    """Find function definitions in src/*.c."""
    impls: Set[str] = set()
    for cfile in src_dir.rglob("*.c"):
        lines = read_file(cfile).splitlines()
        for i, line in enumerate(lines):
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("."):
                continue
            if "(" not in line:
                continue
            has_brace = "{" in line
            if not has_brace:
                for j in range(i + 1, min(i + 6, len(lines))):
                    if "{" in lines[j]:
                        has_brace = True
                        break
            if not has_brace:
                continue
            before_paren = line.split("(")[0]
            words = before_paren.split()
            if not words:
                continue
            fn = words[-1].lstrip("*")
            if fn.startswith("fx_"):
                impls.add(fn)
    return impls


def find_tests(test_dir: Path) -> Set[str]:
    """Find which API functions are called in tests."""
    tested: Set[str] = set()
    for cfile in test_dir.glob("*.c"):
        text = read_file(cfile)
        for match in re.finditer(r"\b(fx_\w+)\s*\(", text):
            tested.add(match.group(1))
    return tested


def find_documentation(doc_path: Path) -> Set[str]:
    """Find API functions mentioned in the API reference."""
    if not doc_path.exists():
        return set()
    text = read_file(doc_path)
    documented: Set[str] = set()
    for match in re.finditer(r"\b(fx_\w+)\b", text):
        documented.add(match.group(1))
    return documented


def run_meson_tests(build_dir: Path) -> Tuple[int, int, List[str]]:
    """Run meson test and return (passed, total, failures)."""
    if not build_dir.exists():
        return 0, 0, ["Build directory not found; run `meson setup build` first"]

    try:
        result = subprocess.run(
            [
                "meson", "test",
                "-C", str(build_dir),
                "--print-errorlogs",
                "--num-processes", "1",
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
    except FileNotFoundError:
        return 0, 0, ["`meson` not found in PATH"]
    except subprocess.TimeoutExpired:
        return 0, 0, ["meson test timed out"]

    output = result.stdout + result.stderr
    passed = 0
    total = 0
    failures: List[str] = []

    # Parse "Ok:   11" and "Fail:   0"
    ok_match = re.search(r"Ok:\s+(\d+)", output)
    fail_match = re.search(r"Fail:\s+(\d+)", output)
    if ok_match:
        passed = int(ok_match.group(1))
    if fail_match:
        fail_count = int(fail_match.group(1))
        total = passed + fail_count
        if fail_count:
            failures.append(f"{fail_count} test(s) failed")
    else:
        total = passed

    if result.returncode != 0 and not failures:
        failures.append("meson test exited with non-zero status")

    return passed, total, failures


def build_api_matrix(api: List[Tuple[str, str]], impls: Set[str], tested: Set[str], documented: Set[str]) -> str:
    lines: List[str] = []
    lines.append("| Module | Function | Implemented | Tested | Documented |")
    lines.append("|--------|----------|-------------|--------|------------|")

    modules: Dict[str, List[str]] = {}
    for mod, fn in api:
        modules.setdefault(mod, []).append(fn)

    for mod in sorted(modules.keys()):
        for fn in sorted(modules[mod]):
            impl = "✅" if fn in impls else "❌"
            test = "✅" if fn in tested else "❌"
            doc = "✅" if fn in documented else "❌"
            lines.append(f"| {mod} | `{fn}` | {impl} | {test} | {doc} |")

    return "\n".join(lines)


def score_category(name: str, score: float, gate: float) -> str:
    status = "✅ PASS" if score >= gate else "❌ FAIL"
    return f"{name}: {score:.0f}% (gate {gate:.0f}%) {status}"


def main() -> int:
    parser = argparse.ArgumentParser(description="flux release readiness checker")
    parser.add_argument("--api-matrix", action="store_true", help="Print only the API matrix")
    parser.add_argument("--build-dir", default="build", help="Meson build directory")
    args = parser.parse_args()

    api: List[Tuple[str, str]] = []
    for header_path in sorted(PUBLIC_HEADER_DIR.glob("*.h")):
        api.extend(extract_public_api(read_file(header_path)))

    impls = find_implementations(PROJECT_ROOT / "src")
    tested = find_tests(PROJECT_ROOT / "tests")
    documented = find_documentation(PROJECT_ROOT / "docs" / "reference" / "api.md")

    # Deduplicate
    seen: Set[str] = set()
    unique_api: List[Tuple[str, str]] = []
    for mod, fn in api:
        if fn not in seen:
            seen.add(fn)
            unique_api.append((mod, fn))

    if args.api_matrix:
        print(build_api_matrix(unique_api, impls, tested, documented))
        return 0

    total_api = len(unique_api)
    implemented = sum(1 for _, fn in unique_api if fn in impls)
    tested_count = sum(1 for _, fn in unique_api if fn in tested)
    documented_count = sum(1 for _, fn in unique_api if fn in documented)

    impl_pct = (implemented / total_api * 100) if total_api else 0
    test_pct = (tested_count / total_api * 100) if total_api else 0
    doc_pct = (documented_count / total_api * 100) if total_api else 0

    build_dir = PROJECT_ROOT / args.build_dir
    passed, total_tests, failures = run_meson_tests(build_dir)
    test_suite_pct = (passed / total_tests * 100) if total_tests else 0

    # Check for docs
    readme_ok = (PROJECT_ROOT / "README.md").exists()
    arch_ok = (PROJECT_ROOT / "docs" / "explanation" / "architecture-overview.md").exists()
    roadmap_ok = (PROJECT_ROOT / "docs" / "explanation" / "roadmap.md").exists()
    positioning_ok = (PROJECT_ROOT / "docs" / "explanation" / "positioning.md").exists()

    # CI check
    ci_ok = any((PROJECT_ROOT / d).exists() for d in [".github", ".gitlab-ci.yml"])

    lines: List[str] = []
    lines.append("# Flux Release Readiness Scorecard")
    lines.append("")
    lines.append("Generated automatically by `scripts/check-release-readiness.py`.")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- **Public API symbols:** {total_api}")
    lines.append(f"- **Implemented:** {implemented} ({impl_pct:.0f}%)")
    lines.append(f"- **Tested:** {tested_count} ({test_pct:.0f}%)")
    lines.append(f"- **Documented in docs/reference/api.md:** {documented_count} ({doc_pct:.0f}%)")
    lines.append(f"- **Tests passing:** {passed}/{total_tests} ({test_suite_pct:.0f}%)")
    lines.append("")
    lines.append("## Category Scores")
    lines.append("")
    lines.append(score_category("Functional Completeness", impl_pct, 100.0))
    lines.append(score_category("Test Coverage (API)", test_pct, 90.0))
    lines.append(score_category("Documentation (API reference)", doc_pct, 100.0))
    lines.append(score_category("Test Suite Green", test_suite_pct, 100.0))
    lines.append("")
    lines.append("## Checklist")
    lines.append("")
    lines.append(f"- [ {'x' if readme_ok else ' '} ] README.md exists")
    lines.append(f"- [ {'x' if arch_ok else ' '} ] docs/explanation/architecture-overview.md exists")
    lines.append(f"- [ {'x' if roadmap_ok else ' '} ] docs/explanation/roadmap.md exists")
    lines.append(f"- [ {'x' if positioning_ok else ' '} ] docs/explanation/positioning.md exists")
    lines.append(f"- [ {'x' if ci_ok else ' '} ] CI configuration exists")
    lines.append("")

    if failures:
        lines.append("## Failures")
        lines.append("")
        for f in failures:
            lines.append(f"- ⚠️ {f}")
        lines.append("")

    lines.append("## API Detail Matrix")
    lines.append("")
    lines.append(build_api_matrix(unique_api, impls, tested, documented))
    lines.append("")

    output = "\n".join(lines)
    print(output)

    # Also write to build dir if available
    if build_dir.exists():
        (build_dir / "release-readiness.md").write_text(output)
        print(f"\nScorecard also written to {build_dir / 'release-readiness.md'}")

    return 0 if (impl_pct >= 100 and test_pct >= 90 and doc_pct >= 100 and test_suite_pct >= 100) else 1


if __name__ == "__main__":
    sys.exit(main())
