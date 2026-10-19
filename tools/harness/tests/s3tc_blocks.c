/*
S3TC_BLOCKS.C (test)

The CPU decoders of s3tc.c (DecodeBlockRGB__single_pixel and DecodeBlockAlpha4__single_pixel) in an Xbox map's session on a
big-endian host read a block from a copy made as its little-endian bytes say. The helpers and the decoders are taken from the
source with that branch on (mac_xbox_mode answering 1); a block of random bytes, as the map holds it, must decode as an
independent reference decodes it from the bytes. test_s3tc_blocks.py takes the code; its negative control forgets the byte order.
*/

#include "harness.h"
#include <stdlib.h>

typedef unsigned short word;
#define UNSIGNED_SHORT_MAX 0xFFFF
#define UNSIGNED_LONG_MAX 0xFFFFFFFFUL
static int mac_xbox_mode(void) { return 1; }
#include "types.inc"
#include "under_test.inc"

static unsigned char expand5(unsigned v) { return (unsigned char)((v << 3) | (v >> 2)); }
static unsigned char expand6(unsigned v) { return (unsigned char)((v << 2) | (v >> 4)); }

/* the DXT1 texel (u, v) of the 8 bytes, from the bytes alone: blue, green, red, alpha */
static void reference(const unsigned char *b, int u, int v, unsigned char out[4])
{
	unsigned c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
	unsigned long bits = (unsigned long)b[4] | ((unsigned long)b[5] << 8) | ((unsigned long)b[6] << 16) | ((unsigned long)b[7] << 24);
	unsigned char color[4][4];
	int channel, index = (int)((bits >> (2 * (4 * v + u))) & 3);

	color[0][0] = expand5(c0 & 31); color[0][1] = expand6((c0 >> 5) & 63); color[0][2] = expand5(c0 >> 11); color[0][3] = 255;
	color[1][0] = expand5(c1 & 31); color[1][1] = expand6((c1 >> 5) & 63); color[1][2] = expand5(c1 >> 11); color[1][3] = 255;
	for (channel = 0; channel < 3; channel++)
	{
		if (c0 > c1)
		{
			color[2][channel] = (unsigned char)((2 * color[0][channel] + color[1][channel] + 1) / 3);
			color[3][channel] = (unsigned char)((2 * color[1][channel] + color[0][channel] + 1) / 3);
		}
		else
		{
			color[2][channel] = (unsigned char)((color[0][channel] + color[1][channel]) / 2);
			color[3][channel] = 0;
		}
	}
	color[2][3] = 255;
	color[3][3] = c0 > c1 ? 255 : 0;
	memcpy(out, color[index], 4);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	unsigned char block[16];
	int trial, u, v, i;

	srand(7);

	CASE("dxt1")
	{
		for (trial = 0; trial < 400; trial++)
		{
			for (i = 0; i < 8; i++) block[i] = (unsigned char)(rand() >> 4);
			for (v = 0; v < 4; v++)
				for (u = 0; u < 4; u++)
				{
					struct s3tc_color got;
					unsigned char want[4];

					DecodeBlockRGB__single_pixel((struct s3tc_block_rgb const *)block, &got, (short)u, (short)v);
					reference(block, u, v, want);
					CHECK(!memcmp(got.rgba, want, 4), "trial %d texel %d,%d: %02x%02x%02x%02x against %02x%02x%02x%02x", trial, u, v,
						got.rgba[0], got.rgba[1], got.rgba[2], got.rgba[3], want[0], want[1], want[2], want[3]);
				}
		}
	}

	CASE("dxt3-alpha")
	{
		for (trial = 0; trial < 200; trial++)
		{
			for (i = 0; i < 16; i++) block[i] = (unsigned char)(rand() >> 4);
			for (v = 0; v < 4; v++)
				for (u = 0; u < 4; u++)
				{
					struct s3tc_color got;
					unsigned alpha = (block[2 * v] | (block[2 * v + 1] << 8)) >> (4 * u) & 0xF;

					DecodeBlockAlpha4__single_pixel((struct s3tc_block_alpha4 const *)block, &got, (short)u, (short)v);
					CHECK(got.rgba[3] == (alpha << 4 | alpha), "alpha trial %d texel %d,%d", trial, u, v);
				}
		}
	}

	return 0;
}
