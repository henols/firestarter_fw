"""The one reader for CMake compile databases (compile_commands.json).

Tests that need the definitions of a real build read them here, so every test
accepts the same argv forms and reports a missing entry the same way.
"""
import json
import shlex
from pathlib import Path

from ._host_build import REPO_ROOT

COMPILE_COMMANDS_ENV = "FIRESTARTER_PY32_COMPILE_COMMANDS"


def defines_from_argv(tokens):
    """The -D definitions in a compiler argv, without the -D, in both the
    `-DX` and the `-D X` form."""
    defines = []
    for index, token in enumerate(tokens):
        if token == "-D" and index + 1 < len(tokens):
            defines.append(tokens[index + 1])
        elif token.startswith("-D") and len(token) > 2:
            defines.append(token[2:])
    return defines


def _entries(database):
    for entry in json.loads(Path(database).read_text()):
        tokens = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
        yield (Path(entry["directory"]) / entry["file"]).resolve(), tokens


def compile_defines(database, source):
    """The definitions of the entry that compiles `source`, a path relative to
    the repository root. Raises LookupError that names the database and the
    source when no entry compiles it."""
    wanted = (REPO_ROOT / source).resolve()
    for path, tokens in _entries(database):
        if path == wanted:
            return defines_from_argv(tokens)
    raise LookupError(f"{database} has no compile entry for {source}")


def firmware_defines(database):
    """(repository-relative source, definitions) for every entry that compiles
    a firmware source of this repository; vendor SDK sources are left out."""
    root = REPO_ROOT.resolve()
    result = []
    for path, tokens in _entries(database):
        try:
            relative = path.relative_to(root)
        except ValueError:
            continue
        if relative.parts[0] in ("src", "platform"):
            result.append((relative.as_posix(), defines_from_argv(tokens)))
    return result
