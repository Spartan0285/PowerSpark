#ifndef HALO_MAC_GPU_PICK_H
#define HALO_MAC_GPU_PICK_H
/* What the Mac build chooses for a machine when its settings say "auto":
   pure functions of the video memory (megabytes), the CPU and the map, so
   they are tested without a graphics card (tools/mac_gpu_pick_probe.c). */
#include <string.h>

enum { MAC_DETAIL_HIGH, MAC_DETAIL_LOW, MAC_DETAIL_LOWEST, MAC_DETAIL_MEDIUM };

/* Detail for mac.detail "auto": a PowerPC Mac with 64 MB of video memory or
   less (the iBook's Radeon 9550, the PowerBook's Radeon 9600 and 9700) draws
   the low preset: the medium preset ran the PowerBook's Radeon 9700 at about
   10 frames a second in b30, and the low preset is the one the 30 frame work
   was measured with; an Intel Mac, or a PowerPC Mac with a larger card, the
   stock detail. */
static __inline__ int mac_gpu_pick_preset(int vram_mb, int is_ppc)
{
    if (!is_ppc) return MAC_DETAIL_HIGH;
    return vram_mb <= 64 ? MAC_DETAIL_LOW : MAC_DETAIL_HIGH;
}

/* The picture's height in lines for mac.render_resolution "auto" (0: the
   window's own, "native"). An Intel Mac with a card of more than 64 MB draws
   at the window's pixels; a PowerPC Mac (fill rate is the iBook's limit, and
   the PowerBook's) and the small Intel cards (GMA 950) draw the Xbox's 480
   lines and the window scales them up. Only the default: every choice of the
   setting is offered on every Mac. */
static __inline__ int mac_gpu_pick_render_lines(int vram_mb, int is_ppc)
{
    return is_ppc || vram_mb <= 64 ? 480 : 0;
}

/* The levels of the campaign (their map names), which hold far more texture
   than a multiplayer map */
static __inline__ int mac_gpu_map_is_campaign(const char *name)
{
    static const char *const levels[] = { "a10", "a30", "a50", "b30", "b40",
        "c10", "c20", "c40", "d20", "d40", 0 };
    int index;
    for (index = 0; levels[index]; ++index)
        if (name && !strcmp(name, levels[index])) return 1;
    return 0;
}

/* The maps whose textures do not fit a small card's cache at full size */
static __inline__ int mac_gpu_map_is_heavy(const char *name)
{
    return name && (mac_gpu_map_is_campaign(name) || !strcmp(name, "longest") ||
        !strcmp(name, "hangemhigh") || !strcmp(name, "prisoner"));
}

/* The texture cache's megabytes for mac.texture_cache_mb 0 */
static __inline__ int mac_gpu_pick_texture_cache_mb(int vram_mb, int campaign)
{
    if (vram_mb < 64) return 16;
    return campaign ? 48 : 32;
}

/* The largest bitmap levels left out for mac.texture_skip_levels -1: one on a card of 32 MB or less
   for the maps that do not fit its cache at full size, and for any map when the cache is short of the
   room an ordinary map wants (cache_mb under 12; 0: not known) */
static __inline__ int mac_gpu_pick_texture_skip_levels_for(int vram_mb, const char *map, int cache_mb)
{
    if (vram_mb > 32) return 0;
    return mac_gpu_map_is_heavy(map) || (cache_mb > 0 && cache_mb < 12) ? 1 : 0;
}
static __inline__ int mac_gpu_pick_texture_skip_levels(int vram_mb, const char *map)
{
    return mac_gpu_pick_texture_skip_levels_for(vram_mb, map, 0);
}

/* The texture cache of a card of 32 MB or less, from the video memory the system says is free when the
   textures begin (free_mb, negative: not known): what is free less 10 MB for what is made later (the
   frame's render targets, the world's vertex buffers, the menus' pictures), at least 8 MB and not more
   than the card's pick. */
static __inline__ int mac_gpu_pick_texture_cache_from_free(int picked_mb, int vram_mb, long free_mb)
{
    long budget;
    if (vram_mb > 32 || free_mb < 0) return picked_mb;
    budget = free_mb - 10;
    if (budget < 8) budget = 8;
    return budget < picked_mb ? (int)budget : picked_mb;
}

/* How the Xbox maps' uncompressed textures are held (mac.texture_native_depth -1): the 16-bit formats at
   two bytes on a card of 32 MB or less, as MacSoft's Halo holds them (GL_RGB5, GL_RGB5_A1, GL_RGBA4),
   else only the 8-bit formats (the picture the same) */
static __inline__ int mac_gpu_pick_texture_native_depth(int vram_mb)
{
    return vram_mb <= 32 ? 2 : 1;
}

/* The video memory of a card by the name its driver gives (the answer when
   the system does not say), for the cards the Macs had; 32 when unknown */
static __inline__ int mac_gpu_vram_from_name(const char *renderer)
{
    static const struct { const char *name; int mb; } cards[] = {
        { "Radeon 7500", 32 }, { "Radeon 9200", 32 }, { "Radeon 9550", 32 },
        { "Radeon 9600", 64 }, { "Radeon 9700", 64 }, { "Radeon 9800", 128 },
        { "Radeon X1600", 128 }, { "GeForce FX 5200", 64 },
        { "GeForce 6600", 128 }, { "GMA 950", 64 }, { "GMA X3100", 128 },
        { "GeForce 9400", 256 }, { "GeForce 9600", 256 }, { 0, 0 } };
    int index;
    if (!renderer) return 32;
    for (index = 0; cards[index].name; ++index)
        if (strstr(renderer, cards[index].name)) return cards[index].mb;
    return 32;
}
#endif
