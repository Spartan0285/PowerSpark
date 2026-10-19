/*
NETWORK_GAME_WIRE.C (test)

The game's settings go over the wire as the raw bytes of struct network_game, little-endian. network_game_wire_swap
(source/networking/network_game_manager.c) swaps its fields to and from that order on a big-endian host (the Mac's
PowerPC), whose G4 would otherwise read the Intel host's settings reversed. The function is taken from the source, with the
big-endian host's body (halo_wire_swap_always does the same on any host). The structure is built as the wire carries it,
swapped to the host's order, and read with a big-endian host's reads; swapped back it must be the wire's bytes again.
*/

#include "harness.h"
#include "snippets.inc"

static unsigned short be_short(const void *at) { const unsigned char *b = at; return (unsigned short)(b[0] << 8 | b[1]); }
static unsigned long be_long(const void *at) { const unsigned char *b = at; return (unsigned long)b[0] << 24 | (unsigned long)b[1] << 16 | (unsigned long)b[2] << 8 | b[3]; }
static void put_le_short(void *at, unsigned short v) { unsigned char *b = at; b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8); }
static void put_le_long(void *at, unsigned long v) { unsigned char *b = at; b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8); b[2] = (unsigned char)(v >> 16); b[3] = (unsigned char)(v >> 24); }

static struct network_game wire, original;

static void build(long engine)
{
	int i;

	memset(&wire, 0, sizeof(wire));
	put_le_short(&wire.name[0], 0x0041);
	put_le_short(&wire.name[15], 0x0042);
	put_le_long(&wire.map.version, 0x01020304UL);
	wire.map.name[0] = 'b';
	put_le_short(&wire.variant.human_readable_game_description[11], 0x0043);
	put_le_long(&wire.variant.game_engine_index, (unsigned long)engine);
	put_le_long(&wire.variant.universal_variant.flags, 0x11223344UL);
	put_le_long(&wire.variant.universal_variant.score_to_win, 15);
	put_le_long(&wire.variant.universal_variant.vehicle_set, 7);
	wire.variant.universal_variant.teams = 1;
	wire.variant.universal_variant.odd_man_out = 1;
	switch (engine)
	{
	case game_engine_ctf:
		wire.variant.game_engine_variant.ctf.assault = 1;
		put_le_long(&wire.variant.game_engine_variant.ctf.single_flag_time, 0x01020304UL);
		break;
	case game_engine_oddball:
		wire.variant.game_engine_variant.oddball.random_start = 1;
		put_le_long(&wire.variant.game_engine_variant.oddball.ball_spawn_count, 9);
		put_le_long(&wire.variant.game_engine_variant.oddball.speed_with_ball, 0x0A0B0C0DUL);
		break;
	case game_engine_race:
		put_le_long(&wire.variant.game_engine_variant.race.race_type, 2);
		put_le_long(&wire.variant.game_engine_variant.race.team_scoring, 1);
		break;
	case game_engine_slayer:
		wire.variant.game_engine_variant.slayer.no_kill_penalty = 1;
		break;
	}
	put_le_short(&wire.variant.flags, 0x0105);
	wire.cooperative_flags = 3;
	wire.minimum_players = 1;
	wire.maximum_players = 16;
	put_le_short(&wire.difficulty, 2);
	put_le_short(&wire.machine_count, 2);
	put_le_short(&wire.machines[1].name[0], 0x0050);
	wire.machines[1].machine_index = 1;
	put_le_short(&wire.player_count, 2);
	put_le_short(&wire.players[1].name[0], 0x0051);
	put_le_short(&wire.players[1].primary_color_index, 3);
	put_le_short(&wire.players[1].icon_index, 4);
	wire.players[1].machine_index = 1;
	wire.players[1].team_index = 1;
	put_le_short(&wire.players[127].icon_index, 9);
	put_le_short(&wire.__unknown426, 6);
	put_le_long(&wire.random_seed, 0x99887766UL);
	put_le_long(&wire.number_of_games_played, 5);
	put_le_short(&wire.variant_options.time_limit, 30);
	put_le_short(&wire.variant_options.vehicle_respawn_time, 120);
	wire.variant_options.loadout = 2;
	for (i = 0; i < 4; i++)
		wire.variant_options.vehicle_counts[0][i] = (byte)i;
	original = wire;
}

static void check_host_view(long engine)
{
	CHECK(be_short(&wire.name[0]) == 0x41 && be_short(&wire.name[15]) == 0x42, "name");
	CHECK(be_long(&wire.map.version) == 0x01020304UL, "map version %08lx", be_long(&wire.map.version));
	CHECK(wire.map.name[0] == 'b', "map name");
	CHECK(be_short(&wire.variant.human_readable_game_description[11]) == 0x43, "description");
	CHECK(be_long(&wire.variant.game_engine_index) == (unsigned long)engine, "engine %ld", (long)be_long(&wire.variant.game_engine_index));
	CHECK(be_long(&wire.variant.universal_variant.flags) == 0x11223344UL, "variant flags");
	CHECK(be_long(&wire.variant.universal_variant.score_to_win) == 15, "score to win");
	CHECK(be_long(&wire.variant.universal_variant.vehicle_set) == 7, "vehicle set");
	CHECK(wire.variant.universal_variant.teams == 1 && wire.variant.universal_variant.odd_man_out == 1, "booleans");
	switch (engine)
	{
	case game_engine_ctf:
		CHECK(wire.variant.game_engine_variant.ctf.assault == 1, "ctf boolean");
		CHECK(be_long(&wire.variant.game_engine_variant.ctf.single_flag_time) == 0x01020304UL, "ctf flag time");
		break;
	case game_engine_oddball:
		CHECK(wire.variant.game_engine_variant.oddball.random_start == 1, "oddball boolean");
		CHECK(be_long(&wire.variant.game_engine_variant.oddball.ball_spawn_count) == 9, "ball count");
		CHECK(be_long(&wire.variant.game_engine_variant.oddball.speed_with_ball) == 0x0A0B0C0DUL, "ball speed");
		break;
	case game_engine_race:
		CHECK(be_long(&wire.variant.game_engine_variant.race.race_type) == 2, "race type");
		CHECK(be_long(&wire.variant.game_engine_variant.race.team_scoring) == 1, "team scoring");
		break;
	case game_engine_slayer:
		CHECK(wire.variant.game_engine_variant.slayer.no_kill_penalty == 1, "slayer boolean");
		break;
	}
	CHECK(be_short(&wire.variant.flags) == 0x0105, "variant flags word");
	CHECK(wire.cooperative_flags == 3 && wire.minimum_players == 1 && wire.maximum_players == 16, "bytes");
	CHECK(be_short(&wire.difficulty) == 2 && be_short(&wire.machine_count) == 2, "difficulty/machines");
	CHECK(be_short(&wire.machines[1].name[0]) == 0x50 && wire.machines[1].machine_index == 1, "machine");
	CHECK(be_short(&wire.player_count) == 2, "player count");
	CHECK(be_short(&wire.players[1].name[0]) == 0x51 && be_short(&wire.players[1].primary_color_index) == 3 &&
		be_short(&wire.players[1].icon_index) == 4, "player fields");
	CHECK(wire.players[1].machine_index == 1 && wire.players[1].team_index == 1, "player bytes");
	CHECK(be_short(&wire.players[127].icon_index) == 9, "last player");
	CHECK(be_short(&wire.__unknown426) == 6, "unknown426");
	CHECK(be_long(&wire.random_seed) == 0x99887766UL && be_long(&wire.number_of_games_played) == 5, "seed and games");
	CHECK(be_short(&wire.variant_options.time_limit) == 30 && be_short(&wire.variant_options.vehicle_respawn_time) == 120,
		"options");
	CHECK(wire.variant_options.loadout == 2 && wire.variant_options.vehicle_counts[0][3] == 3, "option bytes");
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("ctf") { build(game_engine_ctf); SWAP_TO_HOST(&wire); check_host_view(game_engine_ctf); }
	CASE("slayer") { build(game_engine_slayer); SWAP_TO_HOST(&wire); check_host_view(game_engine_slayer); }
	CASE("oddball") { build(game_engine_oddball); SWAP_TO_HOST(&wire); check_host_view(game_engine_oddball); }
	CASE("race") { build(game_engine_race); SWAP_TO_HOST(&wire); check_host_view(game_engine_race); }
	CASE("round-trip")
	{
		long engine;

		for (engine = game_engine_ctf; engine <= game_engine_race; engine++)
		{
			build(engine);
			SWAP_TO_HOST(&wire);
			SWAP_TO_WIRE(&wire);
			CHECK(!memcmp(&wire, &original, sizeof(wire)), "engine %ld: swapped and back is not the wire's bytes", engine);
		}
	}

	return 0;
}
