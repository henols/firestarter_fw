"""Compile production jsmn and prove token ABI and parser behavior.

Unlike the former source-scan contract, this checks the types the compiler
actually sees and tests the parser's signed -1 sentinel and bounded tokens.
The ABI assertions are in C; they are never inferred from Python text scans.
"""

import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jsmn.h"

_Static_assert(_Generic(((jsmntok_t*)0)->type, uint8_t: 1, default: 0),
               "token type must remain uint8_t");
_Static_assert(_Generic(((jsmntok_t*)0)->size, uint8_t: 1, default: 0),
               "token child count must remain uint8_t");
_Static_assert(_Generic(((jsmntok_t*)0)->start, int: 1, default: 0),
               "start offset must remain signed int for -1 sentinel");
_Static_assert(_Generic(((jsmntok_t*)0)->end, int: 1, default: 0),
               "end offset must remain signed int for -1 sentinel");
_Static_assert(offsetof(jsmntok_t, end) > offsetof(jsmntok_t, start),
               "token offsets must have the expected order");

int main(void) {
    const char* valid = "{\"arr\":[1,{\"key\":\"value\"}],\"cmd\":2}";
    jsmntok_t tokens[32] = {{0}};
    jsmn_parser parser;
    jsmn_init(&parser);

    int n = jsmn_parse(&parser, valid, strlen(valid), tokens, 32);
    assert(n > 0);
    assert(tokens[0].type == JSMN_OBJECT);
    assert(tokens[0].size == 2);
    assert(tokens[0].start == 0);
    assert(tokens[0].end == (int)strlen(valid));
    for (int i = 0; i < n; ++i) {
        assert(tokens[i].start >= 0);
        assert(tokens[i].end > tokens[i].start);
        assert(tokens[i].end <= (int)strlen(valid));
    }

    // Invalid partial JSON must be rejected rather than leaving -1 sentinels
    // to masquerade as finished tokens.
    const char* partial = "{\"arr\":[1,";
    jsmn_init(&parser);
    int rc = jsmn_parse(&parser, partial, strlen(partial), tokens, 32);
    assert(rc == JSMN_ERROR_PART);

    // A full token pool must not overwrite adjacent memory on overflow.
    struct {
        jsmntok_t bounded[3];
        uint32_t canary;
    } limited = {{{0}}, 0xA5D3419Eu};
    jsmn_init(&parser);
    rc = jsmn_parse(&parser, valid, strlen(valid), limited.bounded, 3);
    assert(rc == JSMN_ERROR_NOMEM);
    assert(limited.canary == 0xA5D3419Eu);

    puts("PASS tokenizer types, sentinel, nested parsing and token bounds");
    return 0;
}
"""


def test_compiled_json_token_layout_and_behavior(tmp_path: Path) -> None:
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    assert compiler is not None, "a C compiler is required (no silent skip)"

    source = tmp_path / "jsmn_behavior.c"
    source.write_text(SOURCE)
    binary = tmp_path / "jsmn_behavior"
    compile_args = [
        compiler, "-std=c11", "-I", str(ROOT / "lib/jsmn/src"),
        str(source), str(ROOT / "lib/jsmn/src/jsmn.c"), "-o", str(binary),
    ]
    result = subprocess.run(compile_args, cwd=ROOT, capture_output=True, text=True)
    assert result.returncode == 0, (
        f"jsmn compile failed:\n{result.stdout}\n{result.stderr}"
    )
    result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode == 0, (
        f"jsmn runtime failed:\n{result.stdout}\n{result.stderr}"
    )
    assert result.stdout.strip() == (
        "PASS tokenizer types, sentinel, nested parsing and token bounds"
    )
