"""A game advertisement is the raw bytes of its structure on the wire, little-endian. network_game_advertisement_wire_swap
swaps its fields to the host's order on a big-endian host (the Mac's PowerPC), where it read the open flag from the wrong
end. The function and the layout are taken from the sources and run as a little-endian and as a big-endian host runs them;
the negative control gives the layout a wrong field size, which the big-endian run must catch."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["flags-and-counts", "header-and-name", "map", "bytes-untouched"]

PRELUDE = """
#include <stddef.h>
typedef unsigned short word;
typedef unsigned short wchar_t_test;
#define wchar_t wchar_t_test
typedef struct { unsigned char ab[8]; } XNKID;
typedef struct { unsigned char ab[16]; } XNKEY;
typedef struct { unsigned char b[12]; } XNADDR;
struct network_game_map { long version; char name[0x80]; };
enum { TRANSPORT_NONCE_LENGTH = 8, NETWORK_GAME_NAME_LENGTH = 16, JOIN_GAME_TOKEN_LENGTH = 16 };
"""


def generated(big_endian, mutate=False, unswapped=False):
    source = read("source/networking/network_messages.c")
    handler = read("source/networking/network_client_message_handler.c")
    start = handler.index("struct message_server_game_advertise\n{")
    structure = handler[start:handler.index("};", start) + 2]
    at = source.index("#define GAME_ADVERTISE_LAYOUT")
    layout = source[at:source.index("\n", at)]
    if mutate:
        layout = mutated(layout, "X(s,6)", "X(b,12)")
    swap = function(source, "network_game_advertisement_wire_swap")
    # (the host's order: a big-endian host's swap is the always-swap; a little-endian host swaps nothing)
    swap = swap.replace("halo_wire_swap_le(", "halo_wire_swap_always(" if big_endian and not unswapped else "halo_wire_swap_never(")
    if big_endian:
        defs = """
#define halo_wire_swap_never(data, layout, count) ((void)0)
#define SWAP_AS_HOST(a) network_game_advertisement_wire_swap(a)
#define HOST_SHORT(p) be_short(p)
#define HOST_LONG(p) be_long(p)
"""
    else:
        defs = """
#define halo_wire_swap_never(data, layout, count) ((void)0)
#define SWAP_AS_HOST(a) network_game_advertisement_wire_swap(a)
#define HOST_SHORT(p) (*(unsigned short *)(p))
#define HOST_LONG(p) (*(unsigned long *)(p))
"""
    code = '#include "halo_endian.h"\n' + PRELUDE + structure + "\n" + layout + "\n" + defs + swap
    return (("halo_endian.h", read("port/linux/include/halo_endian.h")), ("snippets.inc", code))


@pytest.mark.parametrize("big_endian", [False, True])
@pytest.mark.parametrize("case", CASES)
def test_case(case, big_endian):
    status, output = run(build("advertisement_wire", generated(big_endian)), case)
    assert status == 0, output


def test_negative_control_wrong_layout_fails_big_endian():
    failed = [case for case in CASES if run(build("advertisement_wire", generated(True, mutate=True)), case)[0] != 0]
    assert failed, "a layout with a wrong field size passed every case as a big-endian host runs it"


def test_negative_control_no_swap_fails_big_endian():
    """What the G4 did: the host's bytes read as they are."""
    failed = [case for case in CASES if run(build("advertisement_wire", generated(True, unswapped=True)), case)[0] != 0]
    assert set(failed) >= {"flags-and-counts", "header-and-name", "map"}, failed
