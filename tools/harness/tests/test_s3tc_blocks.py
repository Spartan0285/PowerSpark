"""The CPU S3TC decoders (s3tc.c) in an Xbox session on a big-endian host: blocks are read as their little-endian bytes say.
The code is taken from the source with its big-endian branch on; the reference is the format's arithmetic on the bytes.
The negative control reads the bytes in the other order."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, structure, run  # noqa: E402

CASES = ["dxt1", "dxt3-alpha"]


def generated(fault=None):
    source = read("source/bitmaps/s3tc/s3tc.c")
    header = read("source/bitmaps/s3tc/s3tc.h")
    start = source.index("#if defined(__BIG_ENDIAN__) || defined(__ppc__) || defined(__POWERPC__)\nextern int mac_xbox_mode")
    end = source.index("#endif\n", source.index("#else\n#define S3TC_NATIVE_BLOCK", start)) + len("#endif\n")
    helpers = "#if 1\nextern int mac_xbox_mode(void);" + source[source.index("\n", start + 10):end]
    helpers = helpers.replace("extern int mac_xbox_mode(void); /* port: port/mac/engine_cache_files.c */", "")
    if fault:
        helpers = mutated(helpers, *fault)
    types = (
        "#define S3TC_BLOCK_PIXELS 16\n#define S3TC_ALPHA 3\n"
        "struct s3tc_color { unsigned char rgba[4]; };\n"
        + structure(header, "s3tc_block_rgb") + ";\n" + structure(header, "s3tc_block_alpha4") + ";\n"
        "static void RGBToColor(word const *prgb, struct s3tc_color *pcolor);\n")
    decoders = function(source, "RGBToColor").replace("static void RGBToColor(", "static void RGBToColor(", 1)
    body = decoders + "\n" + function(source, "DecodeBlockRGB__single_pixel") + "\n" + function(source, "DecodeBlockAlpha4__single_pixel") + "\n"
    return (("types.inc", types), ("under_test.inc", helpers + "\n" + body))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("s3tc_blocks", generated()), case)
    assert status == 0, output


def test_negative_control_wrong_byte_order():
    fault = ("native->color0 = (word)(b[0] | (b[1] << 8));", "native->color0 = (word)((b[0] << 8) | b[1]);")
    status, output = run(build("s3tc_blocks", generated(fault)), "dxt1")
    assert status == CHECK_FAILED, f"the wrong byte order passed: {output}"
