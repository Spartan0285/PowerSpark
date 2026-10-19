/*
ADVERTISEMENT_WIRE.C (test)

A host's game advertisement goes on the wire as the raw bytes of its structure, in the host's order, which is
little-endian on every build there is. A big-endian machine (the Mac's PowerPC) read the Intel host's "open" flag from the
wrong end and judged the game not open, forever. network_game_advertisement_wire_swap (source/networking/network_messages.c)
swaps the fields to and from the wire's order on a big-endian host. The test builds the structure as the wire carries it,
swaps it with the function taken from the source as a big-endian host would (halo_wire_swap_always), and reads each field
from the bytes, which a big-endian host reads as the value; the little-endian run swaps nothing.
*/

#include "harness.h"
#include "snippets.inc"

static unsigned short be_short(const void *at) { const unsigned char *b = at; return (unsigned short)(b[0] << 8 | b[1]); }
static unsigned long be_long(const void *at) { const unsigned char *b = at; return (unsigned long)b[0] << 24 | (unsigned long)b[1] << 16 | (unsigned long)b[2] << 8 | b[3]; }
static void put_le_short(void *at, unsigned short v) { unsigned char *b = at; b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8); }
static void put_le_long(void *at, unsigned long v) { unsigned char *b = at; b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8); b[2] = (unsigned char)(v >> 16); b[3] = (unsigned char)(v >> 24); }

static void build(struct message_server_game_advertise *a)
{
	int i;

	memset(a, 0, sizeof(*a));
	for (i = 0; i < 8; i++)
		a->nonce[i] = (byte)(0x10 + i);
	for (i = 0; i < 12; i++)
		((unsigned char *)&a->xnaddr)[i] = (unsigned char)(0x40 + i);
	put_le_short(&a->port, 0x1234);
	put_le_short(&a->version, 0x0005);
	put_le_short(&a->platform, 0x0002);
	put_le_short(&a->game_name[0], 0x0041);
	put_le_short(&a->game_name[15], 0x0042);
	put_le_long(&a->map.version, 0x01020304UL);
	a->map.name[0] = 'b';
	put_le_short(&a->engine_type, 1);
	put_le_short(&a->machine_count, 3);
	put_le_short(&a->player_count, 2);
	put_le_short(&a->maximum_player_count, 16);
	put_le_short(&a->variant_setting, 15);
	put_le_short(&a->flags, 0x0102);
	for (i = 0; i < 16; i++)
		a->join_game_token[i] = (byte)(0x80 + i);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	struct message_server_game_advertise a;

	CHECK(sizeof(a) == 276, "the structure is %d bytes", (int)sizeof(a));
	build(&a);
	SWAP_AS_HOST(&a);

	CASE("flags-and-counts")
	{
		CHECK(HOST_SHORT(&a.flags) == 0x0102, "flags %04x", HOST_SHORT(&a.flags));
		CHECK(HOST_SHORT(&a.machine_count) == 3, "machine_count %d", HOST_SHORT(&a.machine_count));
		CHECK(HOST_SHORT(&a.maximum_player_count) == 16, "maximum %d", HOST_SHORT(&a.maximum_player_count));
		CHECK(HOST_SHORT(&a.variant_setting) == 15, "variant_setting %d", HOST_SHORT(&a.variant_setting));
	}
	CASE("header-and-name")
	{
		CHECK(HOST_SHORT(&a.port) == 0x1234, "port %04x", HOST_SHORT(&a.port));
		CHECK(HOST_SHORT(&a.version) == 5, "version %d", HOST_SHORT(&a.version));
		CHECK(HOST_SHORT(&a.platform) == 2, "platform %d", HOST_SHORT(&a.platform));
		CHECK(HOST_SHORT(&a.game_name[0]) == 0x41 && HOST_SHORT(&a.game_name[15]) == 0x42, "name %04x %04x",
			HOST_SHORT(&a.game_name[0]), HOST_SHORT(&a.game_name[15]));
	}
	CASE("map")
	{
		CHECK(HOST_LONG(&a.map.version) == 0x01020304UL, "map version %08lx", HOST_LONG(&a.map.version));
		CHECK(a.map.name[0] == 'b', "map name changed");
	}
	CASE("bytes-untouched")
	{
		int i;

		for (i = 0; i < 8; i++)
			CHECK(a.nonce[i] == 0x10 + i, "nonce byte %d", i);
		for (i = 0; i < 12; i++)
			CHECK(((unsigned char *)&a.xnaddr)[i] == 0x40 + i, "xnaddr byte %d is %02x", i, ((unsigned char *)&a.xnaddr)[i]);
		for (i = 0; i < 16; i++)
			CHECK(a.join_game_token[i] == 0x80 + i, "token byte %d", i);
	}
	return 0;
}
