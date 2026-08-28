#!/usr/bin/env python3

from __future__ import annotations

import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent
# The floor is what the suite really reaches, so the task refuses a fall rather than an unmet ambition.
COVERAGE_LINE_FLOOR = 81


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

    # The counters of a previous run belong to the sources as they were then, so a report merged with them describes neither build.
    for stale in coverage_context.build_dir.rglob("*.gcda"):
        stale.unlink()

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
    # A clang toolchain writes the same data through a tool of its own, so gcovr is told which one reads it.
    compiler = ""

    for line in (coverage_context.build_dir / "CMakeCache.txt").read_text(encoding="utf-8").split("\n"):
        if line.startswith("CMAKE_CXX_COMPILER_ID:"):
            compiler = line.split("=", 1)[1]

    reader = ["--gcov-executable", "llvm-cov gcov"] if "Clang" in compiler else []
    run([
        gcovr,
        *reader,
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
        str(COVERAGE_LINE_FLOOR),
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


def translation_placeholders() -> dict[str, int]:
    declared: dict[str, int] = {}
    catalogs = sorted(ROOT.glob("plugins/*/*Translations.h")) + [ROOT / "src" / "plugins" / "CoreTranslations.h"]

    for path in catalogs:
        for key, value in re.findall(r"\{QStringLiteral\(\"([a-z0-9.\-]+)\"\), QStringLiteral\(\"((?:[^\"\\\\]|\\\\.)*)\"\)\}", path.read_text(encoding="utf-8")):
            marks = [int(mark) for mark in re.findall(r"%(\d)", value)]
            declared[key] = max(declared.get(key, 0), max(marks) if marks else 0)

    return declared


def given_arguments(text: str, start: int) -> int:
    total = 0
    index = start

    while True:
        opening = re.match(r"\s*\.arg\(", text[index:])
        if opening is None:
            return total
        index += opening.end()
        depth = 1
        pieces = 1

        while index < len(text) and depth:
            character = text[index]
            if character in "([{":
                depth += 1
            elif character in ")]}":
                depth -= 1
                if depth == 0:
                    break
            elif character == "," and depth == 1:
                pieces += 1
            index += 1

        index += 1
        total += pieces


def mismatched_translations() -> list[str]:
    declared = translation_placeholders()
    found: list[str] = []

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h") or path.name.endswith("Translations.h"):
                continue

            text = path.read_text(encoding="utf-8")

            for match in re.finditer(r"translate\(QStringLiteral\(\"([a-z0-9.\-]+)\"\)\)", text):
                key = match.group(1)
                if key in declared and given_arguments(text, match.end()) != declared[key]:
                    found.append(f"{path.relative_to(ROOT)}:{text[:match.start()].count(chr(10)) + 1} {key}")

            # A call that chooses between two sentences gives the same arguments to both, so both must take the same.
            for match in re.finditer(r"translate\([^()]*\?[^()]*QStringLiteral\(\"([a-z0-9.\-]+)\"\)[^()]*:[^()]*QStringLiteral\(\"([a-z0-9.\-]+)\"\)\)", text):
                first = declared.get(match.group(1))
                second = declared.get(match.group(2))
                if first is not None and second is not None and first != second:
                    found.append(f"{path.relative_to(ROOT)}:{text[:match.start()].count(chr(10)) + 1} {match.group(1)} and {match.group(2)}")

    return found


def unguarded_continuations() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")

            for number, line in enumerate(lines, 1):
                for match in re.finditer(r"\.then\(\s*\[([^\]]*)\]", line):
                    if match.group(1).strip():
                        found.append(f"{path.relative_to(ROOT)}:{number}")

    return found


def unlisted_icons() -> list[str]:
    declaration = (ROOT / "src" / "ui" / "Icons.h").read_text(encoding="utf-8")
    accessor = (ROOT / "src" / "ui" / "Icons.cpp").read_text(encoding="utf-8")
    body = re.search(r"enum class IconName[^{]*\{(.*?)\};", declaration, re.S)
    listing = re.search(r"allIconNames\(\)[^{]*\{(.*?)\n\}", accessor, re.S)

    if body is None or listing is None:
        raise RuntimeError("The icon enumeration and its accessor could not be read")

    declared = [match.group(1) for match in re.finditer(r"(\w+)", body.group(1))]
    listed = re.findall(r"IconName::(\w+)", listing.group(1))
    return sorted(set(declared).symmetric_difference(listed))


def inherited_catalogs() -> list[str]:
    found: list[str] = []

    for path in sorted(ROOT.glob("plugins/*/*Translations.h")) + [ROOT / "src" / "plugins" / "CoreTranslations.h"]:
        lines = path.read_text(encoding="utf-8").split("\n")

        for number, line in enumerate(lines, 1):
            if re.search(r"TranslationEntries \w+ = \w+\(\);", line):
                found.append(f"{path.relative_to(ROOT)}:{number}")

    return found


def crowded_scopes() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")
            where = str(path.relative_to(ROOT))

            for index in range(len(lines) - 1):
                head = lines[index].strip()
                tail = lines[index + 1].strip()

                # A namespace, a class and an aggregate open a scope the project deliberately spaces out.
                opens_block = head.endswith("{") and not re.match(r"^(namespace|class|struct|enum|union|extern|template)\b", head) and not head.startswith("} ")

                if opens_block and not tail:
                    found.append(f"{where}:{index + 1} leaves a blank line after the brace that opens the scope")

                if not head and tail.startswith("}") and not tail.startswith("};") and "// namespace" not in tail:
                    found.append(f"{where}:{index + 2} leaves a blank line before the brace that closes the scope")

    return found


def repeated_comments() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")

            for index in range(len(lines) - 1):
                first = lines[index].strip()
                second = lines[index + 1].strip()

                if not first.startswith("//") or not second.startswith("//"):
                    continue
                if first.startswith("// clang-format") or second.startswith("// clang-format"):
                    continue

                shared = difflib.SequenceMatcher(None, first, second).find_longest_match(0, len(first), 0, len(second))

                if shared.size >= 40:
                    found.append(f"{path.relative_to(ROOT)}:{index + 1} says {first[shared.a:shared.a + shared.size].strip()!r} twice")

    return found


def misgrouped_includes() -> list[str]:
    order = {"project": 1, "qt": 2, "platform": 3, "standard": 4}

    def kind(name: str) -> str:
        if name.startswith('"'):
            return "project"
        inner = name[1:-1]
        if inner.startswith("Q"):
            return "qt"
        if "/" in inner or inner.endswith(".h") or inner.endswith(".hpp"):
            return "platform"
        return "standard"

    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            paragraphs: list[list[str]] = []
            current: list[str] = []

            for line in path.read_text(encoding="utf-8").split("\n"):
                match = re.match(r'\s*#include\s+([<"][^>"]+[>"])', line)
                if match:
                    current.append(match.group(1))
                    continue
                if current and (not line.strip() or not line.strip().startswith("#")):
                    paragraphs.append(current)
                    current = []

            if current:
                paragraphs.append(current)

            # The moc translation unit a class declared in a source file needs is generated and closes that file.
            paragraphs = [p for p in paragraphs if not (len(p) == 1 and p[0].endswith('.moc"'))]

            if not paragraphs:
                continue

            where = str(path.relative_to(ROOT))
            ranks: list[int] = []
            mixed = False

            for index, paragraph in enumerate(paragraphs):
                kinds = {kind(name) for name in paragraph}
                if index == 0 and path.suffix == ".cpp" and len(paragraph) == 1 and kinds == {"project"}:
                    ranks.append(0)
                    continue
                if len(kinds) > 1:
                    found.append(f"{where} puts {' and '.join(sorted(kinds))} headers in one group")
                    mixed = True
                    break
                ranks.append(order[kinds.pop()])

            if not mixed and ranks != sorted(ranks):
                found.append(f"{where} orders its include groups {ranks}")

    return found


def unused_declarations() -> list[str]:
    signals: dict[str, str] = {}
    values: dict[str, str] = {}

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*.h")):
            text = path.read_text(encoding="utf-8")
            where = str(path.relative_to(ROOT))

            for block in re.findall(r"signals:(.*?)(?:\n\s*(?:public|private|protected|};))", text, re.S):
                for name in re.findall(r"\bvoid\s+(\w+)\s*\(", block):
                    signals[name] = where

            for name, body in re.findall(r"enum\s+class\s+(\w+)[^{]*\{(.*?)\}", text, re.S):
                for value in re.findall(r"\b([A-Z]\w*)\s*(?:=[^,}]*)?\s*(?:,|$)", body):
                    values[f"{name}::{value}"] = where

    sources = ""

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix in (".cpp", ".h"):
                sources += path.read_text(encoding="utf-8", errors="ignore")

    found = []

    for name, where in sorted(signals.items()):
        if not re.search(r"::" + re.escape(name) + r"\b", sources) and not re.search(r"\bemit\s+" + re.escape(name) + r"\b", sources):
            found.append(f"the signal {name} in {where} is emitted by nothing and connected to nothing")

    for qualified, where in sorted(values.items()):
        if not re.search(r"\b" + re.escape(qualified) + r"\b", sources):
            found.append(f"the value {qualified} in {where} is named by nothing")

    return found


def mismatched_theme_tokens() -> list[str]:
    theme = (ROOT / "src" / "ui" / "Theme.cpp").read_text(encoding="utf-8")
    block = re.search(r"const QVector<QPair<QString, QString>> tokens\{(.*?)\n    \};", theme, re.S)

    if block is None:
        raise RuntimeError("The theme token substitution could not be read")

    declared = set(re.findall(r'QStringLiteral\("(@\w+)"\)', block.group(1)))
    written: set[str] = set()

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*.cpp")):
            if path.name == "Theme.cpp":
                continue
            written.update(re.findall(r"(@[a-zA-Z][a-zA-Z0-9]*)", path.read_text(encoding="utf-8", errors="ignore")))

    # A style sheet writes the unit against the token, so a token is consumed when it opens one of the names that were written.
    consumed = {name for name in declared if any(token.startswith(name) for token in written)}
    substituted = {token for token in written if any(token.startswith(name) for name in declared)}
    found = [f"{name} is substituted and no style sheet consumes it" for name in sorted(declared - consumed)]
    return found + [f"{token} is written and nothing substitutes it" for token in sorted(written - substituted)]


def unreachable_translations() -> list[str]:
    catalogs = sorted(ROOT.glob("plugins/*/*Translations.h")) + [ROOT / "src" / "plugins" / "CoreTranslations.h"]
    declared: dict[str, str] = {}

    for path in catalogs:
        for key in re.findall(r'QStringLiteral\("([a-z0-9-]+\.[a-z0-9-]+\.[a-z0-9-]+)"\)', path.read_text(encoding="utf-8")):
            declared[key] = str(path.relative_to(ROOT))

    sources = ""

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h", ".json") or path.name.endswith("Translations.h"):
                continue
            sources += path.read_text(encoding="utf-8", errors="ignore")

    unreachable = []

    for key, owner in sorted(declared.items()):
        family = key[: key.rfind(".") + 1]
        if key not in sources and family not in sources:
            unreachable.append(f"{key} in {owner}")

    return unreachable


def divergent_backend_conditions() -> list[str]:
    backends = (
        ROOT / "src" / "terminal" / "platform" / "posix" / "PosixPtyBackend.cpp",
        ROOT / "src" / "terminal" / "platform" / "windows" / "ConPtyBackend.cpp",
    )
    spoken: dict[str, set[tuple[str, str]]] = {}

    for path in backends:
        text = path.read_text(encoding="utf-8")
        found = re.findall(r'"(terminal_[a-z_]+)",\s*"([^"]+)"', text)
        found += re.findall(r'QStringLiteral\("(terminal_[a-z_]+)"\),\s*QStringLiteral\("([^"]+)"\)', text)

        for code, message in found:
            spoken.setdefault(code, set()).add((path.name, message))

    divergent = []

    for code, said in sorted(spoken.items()):
        if len({message for _, message in said}) > 1:
            divergent.append(code + " is " + " and ".join(f"{message!r} in {name}" for name, message in sorted(said)))

    return divergent


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


def task_audit(_: Context) -> None:
    unguarded = unprotected_lambdas()

    if unguarded:
        raise RuntimeError("Every lambda is formatted by hand, so these need clang-format markers:\n  " + "\n  ".join(unguarded))

    stray = stray_comments()

    if stray:
        raise RuntimeError("Every comment is a complete sentence sitting on what it explains:\n  " + "\n  ".join(stray))

    mismatched = mismatched_translations()

    if mismatched:
        raise RuntimeError("A sentence is given exactly the arguments it declares, because one it never asked for reaches the reader as a warning:\n  " + "\n  ".join(mismatched))

    unguarded = unguarded_continuations()

    if unguarded:
        raise RuntimeError("A continuation that reaches anything is given the object it reaches as its context, so destroying that object cancels it:\n  " + "\n  ".join(unguarded))

    unlisted = unlisted_icons()

    if unlisted:
        raise RuntimeError("The accessor answers the complete icon set, so the cases that render and compare every icon reach these too:\n  " + "\n  ".join(unlisted))

    inherited = inherited_catalogs()

    if inherited:
        raise RuntimeError("Every language declares the keys it spells, because a catalog built from another one cannot be told from one that forgot a sentence:\n  " + "\n  ".join(inherited))

    crowded = crowded_scopes()

    if crowded:
        raise RuntimeError("A scope begins and ends at its brace, so no blank line sits against either one:\n  " + "\n  ".join(crowded))

    repeated = repeated_comments()

    if repeated:
        raise RuntimeError("Two comments that say the same clause are one comment, because the second explains nothing the first did not:\n  " + "\n  ".join(repeated))

    misgrouped = misgrouped_includes()

    if misgrouped:
        raise RuntimeError("Includes are one group for the header of the file, one for project headers, one for Qt, one for the platform and one for the standard library, in that order:\n  " + "\n  ".join(misgrouped))

    unused = unused_declarations()

    if unused:
        raise RuntimeError("A declaration nothing reaches is one the reader never meets, so it is removed rather than kept:\n  " + "\n  ".join(unused))

    mismatched = mismatched_theme_tokens()

    if mismatched:
        raise RuntimeError("Every theme value a style sheet writes is substituted and every token substituted is written, because either half alone reaches the screen as itself:\n  " + "\n  ".join(mismatched))

    unreachable = unreachable_translations()

    if unreachable:
        raise RuntimeError("A sentence nothing reaches is one the reader never sees, so every key is named by the code or composed from a family it names:\n  " + "\n  ".join(unreachable))

    divergent = divergent_backend_conditions()

    if divergent:
        raise RuntimeError("Two implementations of one backend report a shared condition by one name, because only one of them compiles per platform:\n  " + "\n  ".join(divergent))


# Cppcheck reads what the audits already read, so the audits run first and their findings are the ones a reader acts on.
def task_lint(context: Context) -> None:
    task_audit(context)
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
    "audit": task_audit,
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
    "audit": "Run the audits this project declares for itself",
    "lint": "Run those audits and then Cppcheck against production sources",
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
