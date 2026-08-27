#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent


@dataclass(frozen=True)
class Context:
    configuration: str
    build_dir: Path
    jobs: int
    verbose: bool
    value: str | None = None


def run(command: list[str], *, cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
    printable = " ".join(command)
    print(f"\n> {printable}", flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def executable(name: str) -> str:
    resolved = shutil.which(name)
    if resolved is None:
        raise RuntimeError(f"Required executable was not found: {name}")
    return resolved


def cmake_configure(context: Context, *definitions: str) -> None:
    command = [
        executable("cmake"),
        "-S",
        str(ROOT),
        "-B",
        str(context.build_dir),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={context.configuration}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        *definitions,
    ]
    run(command)


def cmake_build(context: Context, *targets: str) -> None:
    command = [executable("cmake"), "--build", str(context.build_dir), "--parallel", str(context.jobs)]
    if targets:
        command.extend(["--target", *targets])
    if context.verbose:
        command.append("--verbose")
    run(command)


def task_doctor(_: Context) -> None:
    tools = ["cmake", "ninja", "zig", "clang-format", "cppcheck"]
    optional = ["gcovr"]
    for name in tools:
        print(f"{name}: {executable(name)}")
    for name in optional:
        print(f"{name}: {shutil.which(name) or 'not installed'}")


def task_configure(context: Context) -> None:
    cmake_configure(context)


def task_build(context: Context) -> None:
    if not (context.build_dir / "CMakeCache.txt").exists():
        task_configure(context)
    cmake_build(context)


def app_path(context: Context) -> Path:
    if sys.platform == "darwin":
        return context.build_dir / "src" / "SlotDeck.app" / "Contents" / "MacOS" / "SlotDeck"
    suffix = ".exe" if os.name == "nt" else ""
    return context.build_dir / "src" / f"SlotDeck{suffix}"


def task_run(context: Context) -> None:
    task_build(context)
    run([str(app_path(context))])


def task_test(context: Context) -> None:
    cmake_configure(context, "-DSLOTDECK_BUILD_TESTS=ON")
    cmake_build(context)
    command = [
        executable("ctest"),
        "--test-dir",
        str(context.build_dir),
        "--parallel",
        str(context.jobs),
        "--output-on-failure",
        "--no-tests=error",
    ]
    if context.verbose:
        command.append("--verbose")
    run(command)


def task_coverage(context: Context) -> None:
    coverage_context = Context("Debug", ROOT / "build" / "coverage", context.jobs, context.verbose)
    gcovr = executable("gcovr")
    cmake_configure(
        coverage_context,
        "-DSLOTDECK_BUILD_TESTS=ON",
        "-DSLOTDECK_ENABLE_COVERAGE=ON",
    )
    cmake_build(coverage_context)
    run([
        executable("ctest"),
        "--test-dir",
        str(coverage_context.build_dir),
        "--parallel",
        str(coverage_context.jobs),
        "--output-on-failure",
        "--no-tests=error",
    ])
    report_dir = coverage_context.build_dir / "coverage"
    report_dir.mkdir(parents=True, exist_ok=True)
    run([
        gcovr,
        "--root",
        str(ROOT),
        "--filter",
        str(ROOT / "src"),
        "--filter",
        str(ROOT / "plugins"),
        "--exclude-unreachable-branches",
        "--html-details",
        str(report_dir / "index.html"),
        "--xml",
        str(report_dir / "cobertura.xml"),
        "--fail-under-line",
        "100",
        "--fail-under-branch",
        "100",
        str(coverage_context.build_dir),
    ])


def source_files() -> list[str]:
    extensions = {".c", ".cc", ".cpp", ".h", ".hpp"}
    roots = [ROOT / "src", ROOT / "plugins", ROOT / "tests"]
    return [str(path) for base in roots if base.exists() for path in base.rglob("*") if path.suffix in extensions]


def task_format(_: Context) -> None:
    files = source_files()
    if files:
        run([executable("clang-format"), "-i", *files])


def task_format_check(_: Context) -> None:
    files = source_files()
    if files:
        run([executable("clang-format"), "--dry-run", "--Werror", *files])


LAMBDA_PATTERN = re.compile(r"\[([^\]\[]*)\]\s*(\([^)]*\))?\s*(mutable\s*)?(->\s*[A-Za-z_:<>, ]+)?\s*\{")
CAPTURE_PATTERN = re.compile(r"[&=]?\s*[A-Za-z_&=, .*]*")
TEXT_LITERAL_PATTERN = re.compile(r"\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'")


def unprotected_lambdas() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            protected = False

            for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
                stripped = line.strip()

                if stripped == "// clang-format off":
                    protected = True
                    continue

                if stripped == "// clang-format on":
                    protected = False
                    continue

                if protected or stripped.startswith("//"):
                    continue

                for match in LAMBDA_PATTERN.finditer(TEXT_LITERAL_PATTERN.sub('""', line)):
                    capture = match.group(1)

                    if capture and not CAPTURE_PATTERN.fullmatch(capture):
                        continue

                    found.append(f"{path.relative_to(ROOT)}:{number}")

    return found


def stray_comments() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")

            for number, line in enumerate(lines, 1):
                stripped = line.strip()

                if not stripped.startswith("//") or stripped.startswith("// clang-format"):
                    continue

                body = stripped[2:].strip()

                if not body:
                    continue

                where = f"{path.relative_to(ROOT)}:{number}"

                if number < len(lines) and not lines[number].strip():
                    found.append(f"{where} explains nothing, because a blank line follows it")

                if ";" in body[:-1]:
                    found.append(f"{where} divides a sentence with a semicolon")

                if not body.endswith("."):
                    found.append(f"{where} does not end its sentence")

    return found


def task_lint(_: Context) -> None:
    unguarded = unprotected_lambdas()

    if unguarded:
        raise RuntimeError("Every lambda is formatted by hand, so these need clang-format markers:\n  " + "\n  ".join(unguarded))

    stray = stray_comments()

    if stray:
        raise RuntimeError("Every comment is a complete sentence sitting on what it explains:\n  " + "\n  ".join(stray))

    run([
        executable("cppcheck"),
        "--enable=warning,performance,portability",
        "--std=c++20",
        "--suppress=missingIncludeSystem",
        "--suppress=unknownMacro",
        "--error-exitcode=1",
        str(ROOT / "src"),
        str(ROOT / "plugins"),
    ])


def task_sanitize(context: Context) -> None:
    sanitize_context = Context("Debug", ROOT / "build" / "sanitize", context.jobs, context.verbose)
    cmake_configure(
        sanitize_context,
        "-DSLOTDECK_BUILD_TESTS=ON",
        "-DSLOTDECK_ENABLE_SANITIZERS=ON",
    )
    cmake_build(sanitize_context)
    # An instrumented case costs several times the machine of a plain one and some of them start an instrumented child, so the suite runs on half the cores.
    run([
        executable("ctest"),
        "--test-dir",
        str(sanitize_context.build_dir),
        "--parallel",
        str(max(1, sanitize_context.jobs // 2)),
        "--output-on-failure",
        "--no-tests=error",
    ])


def task_package(context: Context) -> None:
    package_context = Context("Release", ROOT / "build" / "release", context.jobs, context.verbose)
    cmake_configure(package_context, "-DSLOTDECK_BUILD_TESTS=OFF")
    cmake_build(package_context, "package")


VERSION_PATTERN = re.compile(r"^(project\(SlotDeck VERSION )(\d+\.\d+\.\d+)( LANGUAGES .*\)$)", re.MULTILINE)


def cmake_setting(name: str) -> str:
    match = re.search(rf'^set\({name} "([^"]+)"\)$', (ROOT / "cmake" / "Version.cmake").read_text(encoding="utf-8"), re.MULTILINE)
    if match is None:
        raise RuntimeError(f"The {name} declaration was not found in cmake/Version.cmake")
    return match.group(1)


def current_version() -> str:
    match = VERSION_PATTERN.search((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError("The project version declaration was not found in CMakeLists.txt")
    return match.group(2)


def task_models(_: Context, value: str | None = None) -> None:
    if not value:
        raise RuntimeError("the models task needs the path of a LiteLLM checkout")
    run([sys.executable, str(ROOT / "scripts" / "import_models.py"), value])


def task_version(_: Context, value: str | None = None) -> None:
    if value is None:
        print(current_version())
        return

    if re.fullmatch(r"\d+\.\d+\.\d+", value) is None:
        raise RuntimeError(f"The version must use the MAJOR.MINOR.PATCH format: {value}")

    path = ROOT / "CMakeLists.txt"
    path.write_text(VERSION_PATTERN.sub(rf"\g<1>{value}\g<3>", path.read_text(encoding="utf-8")), encoding="utf-8")
    print(f"{current_version()}")


BUNDLED_PLUGIN_COUNT = 8


def macos_staged_bundle(build_dir: Path) -> Path:
    staging = build_dir / "_CPack_Packages" / "Darwin" / "DragNDrop" / f"SlotDeck-{current_version()}-Darwin"
    return staging / "SlotDeck.app"


def validate_macos_bundle(bundle: Path) -> None:
    run([executable("codesign"), "--verify", "--deep", "--strict", str(bundle)])

    plugins = sorted((bundle / "Contents" / "PlugIns").glob("libslotdeck-*.dylib"))
    if len(plugins) != BUNDLED_PLUGIN_COUNT:
        raise RuntimeError(f"The bundle contains {len(plugins)} plugins instead of {BUNDLED_PLUGIN_COUNT}")

    helper = bundle / "Contents" / "Frameworks" / "QtWebEngineCore.framework" / "Helpers" / "QtWebEngineProcess.app"
    if not helper.exists():
        raise RuntimeError("The bundle does not contain the Qt WebEngine helper process")

    linkage = subprocess.run([executable("otool"), "-L", str(bundle / "Contents" / "MacOS" / "SlotDeck")], check=True, capture_output=True, text=True).stdout
    if "QtCore.framework" not in linkage:
        raise RuntimeError("The application executable does not link Qt 6 dynamically")


def task_validate_package(context: Context) -> None:
    package_context = Context("Release", ROOT / "build" / "release", context.jobs, context.verbose)
    if sys.platform == "darwin":
        validate_macos_bundle(macos_staged_bundle(package_context.build_dir))
    else:
        suffix = ".zip" if os.name == "nt" else ".tar.gz"
        packages = sorted(package_context.build_dir.glob(f"SlotDeck-*{suffix}"))
        if not packages:
            raise RuntimeError(f"No SlotDeck package with the {suffix} extension was produced")
    print("Package validation succeeded")


def application_data_dir() -> Path:
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / cmake_setting("SLOTDECK_ORGANIZATION_NAME") / cmake_setting("SLOTDECK_PRODUCT_NAME")
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")
        if not base:
            raise RuntimeError("LOCALAPPDATA is not defined")
        return Path(base) / cmake_setting("SLOTDECK_ORGANIZATION_NAME") / cmake_setting("SLOTDECK_PRODUCT_NAME")
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / cmake_setting("SLOTDECK_ORGANIZATION_NAME") / cmake_setting("SLOTDECK_PRODUCT_NAME")


def task_reset_data(context: Context) -> None:
    directory = application_data_dir()
    if not directory.exists():
        print(f"No application data was found at {directory}")
        return

    print(f"This permanently removes the SlotDeck database and every plugin state in {directory}")
    if context.value != "force" and input("Type the word remove to continue: ").strip() != "remove":
        print("The application data was preserved")
        return

    shutil.rmtree(directory)
    print(f"Removed {directory}")


def task_clean(context: Context) -> None:
    if (context.build_dir / "CMakeCache.txt").exists():
        cmake_build(context, "clean")


def task_distclean(_: Context) -> None:
    build_root = ROOT / "build"
    if build_root.exists():
        shutil.rmtree(build_root)


def task_all(context: Context) -> None:
    task_format_check(context)
    task_build(context)
    task_test(context)


TASKS = {
    "all": task_all,
    "build": task_build,
    "clean": task_clean,
    "configure": task_configure,
    "coverage": task_coverage,
    "distclean": task_distclean,
    "doctor": task_doctor,
    "format": task_format,
    "format-check": task_format_check,
    "lint": task_lint,
    "package": task_package,
    "reset-data": task_reset_data,
    "run": task_run,
    "sanitize": task_sanitize,
    "test": task_test,
    "validate-package": task_validate_package,
    "models": task_models,
    "version": task_version,
}

TASK_DESCRIPTIONS = {
    "all": "Run formatting checks, build and tests",
    "build": "Build the application",
    "clean": "Clean the selected build directory",
    "configure": "Configure the selected build directory",
    "coverage": "Generate the coverage reports",
    "distclean": "Remove every build directory",
    "doctor": "Check required and optional development tools",
    "format": "Format all C and C++ source files",
    "format-check": "Validate C and C++ source formatting",
    "lint": "Run Cppcheck against production sources",
    "package": "Create the release package",
    "reset-data": "Remove the application database and every plugin state",
    "run": "Build and run the application",
    "sanitize": "Build and run with sanitizers enabled",
    "test": "Build and run registered tests",
    "validate-package": "Validate the assembled release package",
    "models": "Rebuild the AI model catalog from a LiteLLM checkout given as the value",
    "version": "Print the application version or set a new MAJOR.MINOR.PATCH value",
}


def print_tasks() -> None:
    width = max(len(name) for name in TASKS)
    print("Available tasks:\n")
    for name in sorted(TASKS):
        print(f"  {name:<{width}}  {TASK_DESCRIPTIONS[name]}")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="SlotDeck development tasks")
    parser.add_argument("task", choices=sorted(TASKS), nargs="?")
    parser.add_argument("value", nargs="?")
    parser.add_argument("--configuration", choices=["Debug", "Release", "RelWithDebInfo"], default="Debug")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.task is None:
        print_tasks()
        return 0

    build_dir = arguments.build_dir or ROOT / "build" / arguments.configuration.lower()
    context = Context(arguments.configuration, build_dir.resolve(), arguments.jobs, arguments.verbose, arguments.value)
    try:
        if arguments.task == "version":
            task_version(context, arguments.value)
        elif arguments.task == "models":
            task_models(context, arguments.value)
        else:
            TASKS[arguments.task](context)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"\nTask failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
