"""engine_texels_little_endian (mac_gpu_textures.c, its big-endian branch): engine-written (D3D-created) 16 and 32 bit texels
are swapped in their own width for the decoder; a map's, an 8 bit and a compressed texture are not. The negative control
drops the D3D-created test, which swaps a map's texture."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["engine-16-bit", "engine-32-bit", "left-as-they-are"]


def generated(fault=None):
    source = read("port/mac/mac_gpu_textures.c")
    start = source.index("static const unsigned char *engine_texels_little_endian(")
    code = source[start:source.index("#else\n#define engine_texels_little_endian", start)]
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("engine_texels", generated()), case)
    assert status == 0, output


def test_negative_control_no_created_test():
    fault = ("if (!(native->Common & D3DCOMMON_D3DCREATED) || description->compressed) return texels;",
             "if (description->compressed) return texels;")
    status, output = run(build("engine_texels", generated(fault)), "left-as-they-are")
    assert status == CHECK_FAILED, f"a map's texture was swapped and the test did not see it: {output}"
