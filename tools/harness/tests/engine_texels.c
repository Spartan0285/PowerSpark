/*
ENGINE_TEXELS.C (test)

engine_texels_little_endian (mac_gpu_textures.c): a D3D-created texture (the font's glyph cache) is filled with the engine's
own stores in the host's byte order; on a big-endian host its 16 and 32 bit texels are swapped in their own width before the
shared decoder reads them as the console stores them. A texture the map supplies (not D3D-created), an 8 bit one and a
compressed one are read as they are. test_engine_texels.py takes the function (the big-endian branch) from the source.
*/

#include "harness.h"
#include <stdlib.h>

#define D3DCOMMON_D3DCREATED 0x00040000
typedef struct { unsigned long Common; } D3DBaseTexture;
struct xgpu_texture_description { int compressed; unsigned long format; };
struct format_information { unsigned bytes; };
static struct format_information format_information(unsigned long format)
{
	struct format_information information;
	information.bytes = format == 4 ? 2 : format == 6 ? 4 : 1;
	return information;
}
#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	D3DBaseTexture engine = { D3DCOMMON_D3DCREATED }, map = { 0 };
	struct xgpu_texture_description a4r4g4b4 = { 0, 4 }, a8r8g8b8 = { 0, 6 }, p8 = { 0, 1 }, dxt = { 1, 4 };
	const unsigned char texels[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	const unsigned char *got;

	CASE("engine-16-bit")
	{
		got = engine_texels_little_endian(&engine, &a4r4g4b4, texels, 8);
		CHECK(got[0] == 2 && got[1] == 1 && got[2] == 4 && got[3] == 3 && got[6] == 8 && got[7] == 7, "pairs not swapped");
	}

	CASE("engine-32-bit")
	{
		got = engine_texels_little_endian(&engine, &a8r8g8b8, texels, 8);
		CHECK(got[0] == 4 && got[1] == 3 && got[2] == 2 && got[3] == 1 && got[4] == 8 && got[7] == 5, "words not swapped");
	}

	CASE("left-as-they-are")
	{
		CHECK(engine_texels_little_endian(&map, &a4r4g4b4, texels, 8) == texels, "a map's texture was swapped");
		CHECK(engine_texels_little_endian(&engine, &p8, texels, 8) == texels, "an 8 bit texture was swapped");
		CHECK(engine_texels_little_endian(&engine, &dxt, texels, 8) == texels, "a compressed texture was swapped");
	}

	return 0;
}
