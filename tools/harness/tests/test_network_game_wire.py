"""The game's settings record is the raw bytes of struct network_game on the wire, little-endian. network_game_wire_swap
swaps its fields to the host's order on a big-endian host (the Mac's PowerPC). The function and the structures are taken
from the sources; the swap is run with the big-endian host's body (the always-swap does the same on any host) and the
fields read as a big-endian host reads them. The negative control leaves the union's engine part out."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["ctf", "slayer", "oddball", "race", "round-trip"]

PRELUDE = """
#include <stddef.h>
typedef unsigned short word;
typedef unsigned short wchar_test;
#define wchar_t wchar_test
typedef long real_test;
#define real float
#define boolean byte
enum { HALO_PORT_MAXIMUM_NETWORK_MACHINES = 128, HALO_PORT_MAXIMUM_NETWORK_PLAYERS = 128 };
"""


def structure(source, header, name):
    at = source.index(f"struct {name}\n{{")
    return source[at:source.index("\n};", at) + 3]


def generated(mutate=False):
    engine = read("source/game/game_engine.h")
    manager = read("source/networking/network_game_manager.h")
    players = read("source/game/players.h")
    text = read("source/networking/network_game_manager.c")
    enums = engine[engine.index("enum game_engine_type"):]
    enums = enums[:enums.index("};") + 2]
    vehicles = "enum { NUMBER_OF_VARIANT_VEHICLES = 6 };\n"
    code = PRELUDE + enums + vehicles
    for name in ("universal_variant", "ctf_variant", "slayer_variant", "king_variant", "oddball_variant", "race_variant"):
        code += structure(engine, "game_engine.h", name) + "\n"
    at = engine.index("union game_engine_variant\n{")
    code += engine[at:engine.index("\n};", at) + 3] + "\n"
    code += structure(engine, "", "game_variant") + "\n" + structure(engine, "", "game_variant_options") + "\n"
    code += structure(manager, "", "network_machine") + "\n" + structure(manager, "", "network_game_map") + "\n"
    code += structure(manager, "", "network_game_local_data") + "\n" + structure(players, "", "network_player") + "\n"
    code += structure(manager, "", "network_game") + "\n"
    code += "#define HALO_BIG_ENDIAN_HOST 1\n#include \"halo_endian.h\"\n"
    swap = function(text, "network_game_wire_swap")
    if mutate:
        swap = mutated(swap, "case game_engine_oddball:", "case 99:")
    # (this host is little-endian: what the function reads as the host's value is read as a big-endian host reads it)
    swap = swap.replace("engine_index = game->variant.game_engine_index;", "engine_index = (long)be_long(&game->variant.game_engine_index);")
    code += "static unsigned long be_long(const void *at);\n" + swap + "\n"
    code += "#define SWAP_TO_HOST(g) network_game_wire_swap(g, 1)\n#define SWAP_TO_WIRE(g) network_game_wire_swap(g, 0)\n"
    return (("halo_endian.h", read("port/linux/include/halo_endian.h")), ("snippets.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("network_game_wire", generated()), case)
    assert status == 0, output


def test_negative_control_missing_engine_layout_fails():
    assert run(build("network_game_wire", generated(True)), "oddball")[0] == CHECK_FAILED
