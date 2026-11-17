# version_from_git.py — PlatformIO pre-build SCons hook (extra_scripts = pre:)
#
# Defines VERSION from the git tags, so no committed file holds the version.
#
#   HEAD is tag 3.2.0b1           -> "3.2.0b1"
#   HEAD is 5 commits after it    -> "3.2.0b2.dev5"   (a PEP 440 pre-release
#                                    that sorts above the tag, as setuptools-scm
#                                    does for the host)
#   no git, or no release tag     -> nothing defined; include/version.h's
#                                    fallback applies
#
# Only tags that start with a digit are release tags. This keeps the old
# milestone tags (v1.21) and archive/* tags out.

import re
import subprocess

Import("env")


def _next_version(tag):
    """Bump the last number of a release tag: 3.2.0b1 -> 3.2.0b2, 3.2.0 -> 3.2.1."""
    m = re.match(r"^(.*?)(\d+)$", tag)
    return "%s%d" % (m.group(1), int(m.group(2)) + 1)


def _version_from_git():
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--long", "--match", "[0-9]*"],
            cwd=env["PROJECT_DIR"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    m = re.match(r"^(\d+\.\d+\.\d+(?:(?:b|rc)\d+)?)-(\d+)-g[0-9a-f]+$", out)
    if not m:
        return None
    tag, distance = m.group(1), int(m.group(2))
    if distance == 0:
        return tag
    return "%s.dev%d" % (_next_version(tag), distance)


version = _version_from_git()
if version:
    env.Append(CPPDEFINES=[("VERSION", '\\"%s\\"' % version)])
    print("version_from_git.py: VERSION=%s" % version)
