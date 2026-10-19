"""A connection's far end and a peer's real address, between a socket's network order and the host's number, run as a
little-endian and as a big-endian host runs them (the Mac's PowerPC). The negative control is the old inline swap, which the
big-endian run must catch."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, read, run  # noqa: E402

CASES = ["endpoint-address", "endpoint-port", "real-address"]

OLD_ADDRESS = """address->address.long_words[0] =
					(((network_address & 0xFF0000) | (network_address >> 16)) >> 8) |
					(((network_address & 0xFF00) | (network_address << 16)) << 8);"""
OLD_PORT = "address->port = (word)((network_port << 8) | (network_port >> 8));"


def generated(big_endian, old=False):
    transport = read("source/networking/../bungie_net/network/transport_endpoint_winsock.c")
    start = transport.index("address->address.long_words[0] = halo_be32(network_address);")
    address_statement = transport[start:transport.index(";", start) + 1]
    start = transport.index("address->port = halo_be16(network_port);")
    port_statement = transport[start:transport.index(";", start) + 1]
    if old:
        address_statement, port_statement = OLD_ADDRESS, OLD_PORT
    distributed = function(read("port/linux/game/network_distributed.c"), "distributed_real_address")
    if big_endian:
        # (a big-endian machine's socket value of the bytes 100.94.51.88 and of port bytes 13 BE, and the identity conversions)
        defs = """
#define halo_be32(q) ((unsigned long)(q))
#define halo_be16(q) ((unsigned short)(q))
#define SOCKET_ADDRESS 0x645E3358UL
#define SOCKET_PORT 0x13BE
#define PEER_NETWORK(q) ((unsigned long)(q))
"""
    else:
        defs = """
#define halo_be32(q) ((((unsigned long)(q) & 0xFF) << 24) | (((unsigned long)(q) & 0xFF00) << 8) | (((unsigned long)(q) >> 8) & 0xFF00) | (((unsigned long)(q) >> 24) & 0xFF))
#define halo_be16(q) ((unsigned short)((((q) & 0xFF) << 8) | (((q) >> 8) & 0xFF)))
#define SOCKET_ADDRESS 0x58335E64UL
#define SOCKET_PORT 0xBE13
#define PEER_NETWORK(q) halo_be32(q)
"""
    code = defs + """
struct transport_address { struct { unsigned long long_words[1]; } address; unsigned short port; unsigned short address_length; };
enum { IPV4_ADDRESS_LENGTH = 4 };
typedef unsigned short word;
static unsigned long p2p_peer_endpoint_address(unsigned long network)
{
	return network == PEER_NETWORK(0x64400102UL) ? PEER_NETWORK(0xC0A84433UL) : 0;
}
static void fill_endpoint_address(struct transport_address *address)
{
	unsigned long network_address = SOCKET_ADDRESS;
	unsigned short network_port = SOCKET_PORT;

	""" + address_statement + """
	address->address_length = IPV4_ADDRESS_LENGTH;
	""" + port_statement + """
}
""" + distributed + "\n"
    return (("snippets.inc", code),)


@pytest.mark.parametrize("big_endian", [False, True])
@pytest.mark.parametrize("case", CASES)
def test_case(case, big_endian):
    status, output = run(build("transport_address_order", generated(big_endian)), case)
    assert status == 0, output


@pytest.mark.parametrize("case", ["endpoint-address", "endpoint-port"])
def test_negative_control_old_swap_fails_big_endian(case):
    status, output = run(build("transport_address_order", generated(True, old=True)), case)
    assert status == CHECK_FAILED, f"the old inline swap passed '{case}' as a big-endian machine runs it: {output}"


@pytest.mark.parametrize("case", ["endpoint-address", "endpoint-port"])
def test_old_swap_passes_little_endian(case):
    status, output = run(build("transport_address_order", generated(False, old=True)), case)
    assert status == 0, output
