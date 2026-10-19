/* What the Mac build knows of the machine: the video memory and the CPU,
   and from them what the settings "auto" mean (mac.detail,
   mac.texture_cache_mb, mac.texture_skip_levels). */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mac_gpu_info.h"
#include "mac_adaptive_detail.h"
#include "mac_render_scale.h"
#include <mach/mach_time.h>

/* port/linux/src/port_config.c, when linked */
extern const char *config_string(const char *name) __attribute__((weak));
extern long config_integer(const char *name) __attribute__((weak));
extern unsigned long config_changes(void) __attribute__((weak));
extern int config_boolean(const char *name) __attribute__((weak));
/* port/mac/mac_window.c, when linked */
extern int mac_window_swap_interval(void) __attribute__((weak));
extern int mac_window_set_swap_interval(int interval) __attribute__((weak));
extern int mac_window_display_refresh(void) __attribute__((weak));

static int info_vram_mb;
static int info_resolved;
static char info_renderer[128];
static char info_map[64];
static unsigned long info_serial;
static int info_logged_detail;

int mac_gpu_info_is_ppc(void)
{
#if defined(__ppc__) || defined(__ppc64__)
    return 1;
#else
    return 0;
#endif
}

/* the largest video memory of the accelerated renderers of the main display */
static int vram_from_cgl(void)
{
    CGLRendererInfoObj info = 0;
    GLint count = 0, index, best = 0;
    if (CGLQueryRendererInfo(CGDisplayIDToOpenGLDisplayMask(kCGDirectMainDisplay), &info, &count) != kCGLNoError ||
        !info)
        return 0;
    for (index = 0; index < count; ++index) {
        GLint accelerated = 0, memory = 0;
        if (CGLDescribeRenderer(info, index, kCGLRPAccelerated, &accelerated) != kCGLNoError || !accelerated)
            continue;
        if (CGLDescribeRenderer(info, index, kCGLRPVideoMemory, &memory) != kCGLNoError || memory <= 0)
            continue;
        /* (the answer is in bytes; a value this small is already megabytes) */
        if (memory >= 1048576) memory /= 1048576;
        if (memory > best) best = memory;
    }
    CGLDestroyRendererInfo(info);
    return best;
}

static void info_resolve(void)
{
    const char *renderer = 0;
    int vram;
    if (info_resolved) return;
    if (CGLGetCurrentContext()) renderer = (const char *)glGetString(GL_RENDERER);
    if (renderer) {
        strncpy(info_renderer, renderer, sizeof(info_renderer) - 1);
        info_renderer[sizeof(info_renderer) - 1] = 0;
    }
    vram = vram_from_cgl();
    if (vram > 0) {
        info_vram_mb = vram;
    } else if (renderer) {
        info_vram_mb = mac_gpu_vram_from_name(renderer);
    } else {
        /* no context to ask yet: 32, and ask again later */
        return;
    }
    info_resolved = 1;
    ++info_serial;
    fprintf(stderr, "mac settings: %s, %d MB video memory, %s\n", info_renderer[0] ? info_renderer : "renderer unknown",
        info_vram_mb, mac_gpu_info_is_ppc() ? "PowerPC" : "Intel");
}

int mac_gpu_info_vram_mb(void)
{
    info_resolve();
    return info_resolved ? info_vram_mb : 32;
}

const char *mac_gpu_info_renderer(void)
{
    info_resolve();
    return info_renderer;
}

void mac_gpu_info_set_map(const char *name)
{
    size_t length;
    if (!name) name = "";
    length = strlen(name);
    if (length >= 4 && !strcmp(name + length - 4, ".map")) length -= 4;
    if (length >= sizeof(info_map)) length = sizeof(info_map) - 1;
    memcpy(info_map, name, length);
    info_map[length] = 0;
    ++info_serial;
    mac_adaptive_detail_new_map();
}

int mac_gpu_info_campaign(void)
{
    const char *flag = getenv("HALO_MAC_CAMPAIGN");
    return (flag && flag[0] == '1') || mac_gpu_map_is_campaign(info_map);
}

unsigned long mac_gpu_info_serial(void)
{
    info_resolve();
    return info_serial;
}

/* A number in the environment, for measuring a preset's parts (HALO_MAC_PRESET_<name>); else the preset's */
static double info_preset_number(const char *name, double fallback)
{
    char variable[48];
    const char *value;
    sprintf(variable, "HALO_MAC_PRESET_%s", name);
    value = getenv(variable);
    return value && value[0] ? atof(value) : fallback;
}

int mac_gpu_info_detail(struct mac_detail_preset *preset)
{
    const char *mode = config_string ? config_string("mac.detail") : "auto";
    int level = MAC_DETAIL_HIGH;
    int campaign = mac_gpu_info_campaign();
    if (!mode || !mode[0] || !strcmp(mode, "auto")) {
        level = mac_gpu_pick_preset(mac_gpu_info_vram_mb(), mac_gpu_info_is_ppc());
        if (!info_logged_detail && info_resolved) {
            info_logged_detail = 1;
            fprintf(stderr, "mac settings: detail auto picks %s\n",
                level == MAC_DETAIL_LOW ? "low" : level == MAC_DETAIL_MEDIUM ? "medium" : "high");
        }
    } else if (!strcmp(mode, "medium")) {
        level = MAC_DETAIL_MEDIUM;
    } else if (!strcmp(mode, "low")) {
        level = MAC_DETAIL_LOW;
    } else if (!strcmp(mode, "lowest")) {
        level = MAC_DETAIL_LOWEST;
    }
    if (level == MAC_DETAIL_HIGH) return 0;
    preset->specular = preset->shadows = preset->decals = 0;
    preset->particles = level == MAC_DETAIL_LOWEST ? 2 : 1;
    preset->lod_scale = 1.0;
    preset->particle_pixels = 0.0;
    preset->particle_mode = 0;
    if (level == MAC_DETAIL_MEDIUM) {
        /* the PowerBook: the stock picture less the specular, shadows and decals,
           smaller scenery and the smallest of the effects' particles left out */
        preset->scenery_pixels = campaign ? 24.0 : 8.0;
        preset->object_pixels = 0.0;
        preset->lens_flares = 1;
    } else {
        /* in the campaign, whose levels hold hundreds of small plants and rocks and a
           crowd of objects (c10: 6 ms an iBook G4 frame at 64 and 12), more are left out */
        preset->scenery_pixels = campaign ? 64.0 : 16.0;
        preset->object_pixels = campaign ? 12.0 : 6.0;
        preset->lens_flares = 0;
        /* a particle (dust, smoke) is drawn no wider than 160 pixels: one close to the camera
           covers the screen, more pixels than a Radeon 9550 fills in a frame (4.5 ms on Damnation) */
        preset->particle_pixels = 160.0;
    }
    preset->scenery_pixels = info_preset_number("SCENERY", preset->scenery_pixels);
    preset->object_pixels = info_preset_number("OBJECT", preset->object_pixels);
    preset->lod_scale = info_preset_number("LOD", preset->lod_scale);
    preset->lens_flares = info_preset_number("FLARES", preset->lens_flares) != 0.0;
    preset->particle_pixels = info_preset_number("PARTICLE", preset->particle_pixels);
    preset->particle_mode = (int)info_preset_number("PARTICLE_MODE", preset->particle_mode);
    return 1;
}

long mac_gpu_info_texture_cache_mb(void)
{
    long megabytes = config_integer ? config_integer("mac.texture_cache_mb") : 0;
    if (megabytes >= 4 && megabytes <= 48) return megabytes;
    if (megabytes) fprintf(stderr, "mac settings: mac.texture_cache_mb must be 4 to 48; using auto\n");
    megabytes = mac_gpu_pick_texture_cache_mb(mac_gpu_info_vram_mb(), mac_gpu_info_campaign());
    fprintf(stderr, "mac settings: texture cache %ld MB\n", megabytes);
    return megabytes;
}

int mac_gpu_info_texture_skip_levels(void)
{
    long levels = config_integer ? config_integer("mac.texture_skip_levels") : -1;
    if (levels >= 0 && levels <= 4) return (int)levels;
    levels = mac_gpu_pick_texture_skip_levels(mac_gpu_info_vram_mb(), info_map);
    if (levels) fprintf(stderr, "mac settings: texture levels skipped: %ld\n", levels);
    return (int)levels;
}

/* mac.texture_native_depth: 0 as before (every texture four bytes a texel), 1 the 8-bit formats and grey palettes
   at their own depth (picture identical), 2 the 16-bit formats too; -1 picks 1 */
int mac_gpu_info_texture_native_depth(void)
{
    long mode = config_integer ? config_integer("mac.texture_native_depth") : -1;
    if (mode >= 0 && mode <= 2) return (int)mode;
    return 1;
}

/* ---------- mac.adaptive_detail */

static const char *const adaptive_rung_names[MAC_ADAPT_RUNGS] =
{
    "detail objects off", "particles no wider than 96 pixels", "decals off", "lens flares off",
    "scenery and objects: twice the minimum pixels, a lower detail level", "scenery and objects: four times the minimum pixels, two levels lower",
    "planar fog and fog screen off", "particles no wider than 48 pixels", "models at the lowest level of detail",
    "bipeds and vehicles under 12 pixels left out", "bipeds and vehicles under 24 pixels left out",
    "bipeds and vehicles under 48 pixels left out", "480 lines"
};

static struct mac_governor adaptive;
static int adaptive_on;
static double adaptive_fps = 30.0;
static unsigned long adaptive_read_at = (unsigned long)-1;
static unsigned adaptive_render_skipped;
static unsigned long adaptive_serial;
static int adaptive_started;
static int adaptive_resolution_known, adaptive_resolution_possible;
static uint64_t adaptive_last_log;
static unsigned long adaptive_suppressed;
/* HALO_MAC_ADAPTIVE_LEVEL=<0 to 12> (diagnostic): the ladder held at that level, for the gates and for pictures */
static int adaptive_forced = -2;

/* HALO_MAC_ADAPTIVE_MAX_LEVEL=<n> (diagnostic): the ladder has only its first n rungs (8 is the ladder before the model and unit rungs) */
static unsigned adaptive_capped_rungs(void)
{
    static int cap = -2;
    unsigned mask = 0;
    int rung;
    if (cap == -2) {
        const char *value = getenv("HALO_MAC_ADAPTIVE_MAX_LEVEL");
        cap = value && value[0] ? atoi(value) : -1;
    }
    if (cap >= 0)
        for (rung = cap; rung < MAC_ADAPT_RUNGS; ++rung) mask |= 1u << rung;
    return mask;
}

static int adaptive_forced_level(void)
{
    if (adaptive_forced == -2) {
        const char *value = getenv("HALO_MAC_ADAPTIVE_LEVEL");
        adaptive_forced = value && value[0] ? atoi(value) : -1;
        if (adaptive_forced > MAC_ADAPT_RUNGS) adaptive_forced = MAC_ADAPT_RUNGS;
        if (adaptive_forced < -1) adaptive_forced = -1;
    }
    return adaptive_forced;
}

/* the setting: "auto" (on for the PowerPC presets, low and medium, off for the rest), "off", "on" (30 frames a
   second) or a target between 10 and 120 */
static void adaptive_read_setting(void)
{
    const char *value = config_string ? config_string("mac.adaptive_detail") : "auto";
    int on;
    double fps = 30.0;
    if (!value || !value[0] || !strcmp(value, "auto"))
        on = mac_gpu_pick_preset(mac_gpu_info_vram_mb(), mac_gpu_info_is_ppc()) != MAC_DETAIL_HIGH;
    else if (!strcmp(value, "off") || !strcmp(value, "false") || !strcmp(value, "0"))
        on = 0;
    else if (!strcmp(value, "on") || !strcmp(value, "true"))
        on = 1;
    else {
        fps = atof(value);
        on = fps >= 10.0 && fps <= 120.0;
        if (!on) {
            fprintf(stderr, "mac settings: mac.adaptive_detail must be auto, off, on or 10 to 120; using auto\n");
            on = mac_gpu_pick_preset(mac_gpu_info_vram_mb(), mac_gpu_info_is_ppc()) != MAC_DETAIL_HIGH;
            fps = 30.0;
        }
    }
    if (on && (!adaptive_started || fps != adaptive_fps)) {
        mac_governor_reset(&adaptive, fps);
        adaptive_started = 1;
    }
    adaptive_fps = fps;
    adaptive_on = on;
    adaptive_resolution_known = 0;
}

/* whether the last rung would change anything: the frame is drawn with more than 480 lines */
static int adaptive_resolution_available(void)
{
    if (!adaptive_resolution_known) {
        const char *setting = config_string ? config_string("mac.render_resolution") : "auto";
        long lines = mac_render_parse(setting);
        if (lines == MAC_RENDER_LINES_AUTO)
            lines = mac_gpu_pick_render_lines(mac_gpu_info_vram_mb(), mac_gpu_info_is_ppc());
        adaptive_resolution_possible = lines != MAC_RENDER_LINES_MINIMUM;
        adaptive_resolution_known = 1;
    }
    return adaptive_resolution_possible;
}

static void adaptive_refresh(void)
{
    unsigned long changes = config_changes ? config_changes() : 0;
    if (changes != adaptive_read_at || !adaptive_started) {
        int was = adaptive_on;
        adaptive_read_at = changes;
        adaptive_read_setting();
        if (adaptive_on != was || !adaptive_started) {
            ++adaptive_serial;
            if (adaptive_on)
                fprintf(stderr, "adaptive detail: on, target %g frames a second (%.1f ms)\n", adaptive_fps, adaptive.budget_ms);
        }
        if (!adaptive_on && adaptive.level) {
            adaptive.level = 0;
            ++adaptive_serial;
        }
    }
}

int mac_adaptive_detail_enabled(void)
{
    adaptive_refresh();
    return adaptive_on;
}

int mac_adaptive_detail_level(void)
{
    return adaptive_on ? adaptive.level : 0;
}

unsigned long mac_adaptive_detail_serial(void)
{
    return adaptive_serial;
}

void mac_adaptive_detail_set_skipped(unsigned mask)
{
    adaptive_render_skipped = mask;
}

int mac_adaptive_detail_resolution_capped(void)
{
    return adaptive_on && adaptive.level > MAC_ADAPT_RESOLUTION && adaptive_resolution_available();
}

/* seconds: the time since the previous frame; wait_seconds: how much of it a frame rate limiter waited */
/* ---------- the swap's wait (mac_window.c), for the summary */
static double swap_wait_total;
static unsigned long swap_wait_frames;

int mac_adaptive_detail_wall_mode(void)
{
    static int mode = -1;
    if (mode < 0) {
        const char *value = getenv("HALO_MAC_GOV_WALL");
        mode = value && value[0] == '1';
    }
    return mode;
}

void mac_adaptive_detail_note_swap_wait(double seconds)
{
    if (seconds > 0.0) { swap_wait_total += seconds; ++swap_wait_frames; }
}

/* ---------- the swap interval
   With vsync on, a frame's time is a whole number of refreshes whatever its work was: 33.3 ms
   or 50 ms on a 60 Hz panel. On a PowerPC Mac with 32 MB of video memory the target is 30 frames a
   second, so a swap interval of 2 (every second refresh) makes every frame 33.3 ms exactly, the
   cadence of MacSoft's Halo (its frame mode "30 FPS", the default) and the game's own tick, while the
   work fits; when it does not (the average over the target), an interval of 1 loses less (50 ms
   rather than 67) until the work fits again. mac.swap_interval: 0 auto, 1 or 2 fixed. */
static unsigned swap_over_run, swap_under_run;
static int swap_auto_interval;     /* what the auto rule wants now: 0 undecided, 1 or 2 */

static int swap_interval_auto_allowed(void)
{
    int refresh;
    if (!adaptive_on || adaptive_fps != 30.0 || !mac_gpu_info_is_ppc() || mac_gpu_info_vram_mb() > 32) return 0;
    refresh = mac_window_display_refresh ? mac_window_display_refresh() : 0;
    /* (a laptop panel does not say: 60) */
    return refresh == 0 || (refresh >= 58 && refresh <= 62);
}

static void swap_interval_update(void)
{
    long setting = config_integer ? config_integer("mac.swap_interval") : 0;
    int current, want;
    if (!mac_window_swap_interval || !mac_window_set_swap_interval) return;
    current = mac_window_swap_interval();
    if (current <= 0) return;                  /* vsync is off: no interval to choose */
    if (setting >= 1 && setting <= 4) {
        want = (int)setting;
    } else if (swap_interval_auto_allowed()) {
        if (!swap_auto_interval) swap_auto_interval = 2;
        if (adaptive.frames_in_map >= MAC_GOV_WARMUP_FRAMES) {
            if (adaptive.average_ms > adaptive.budget_ms * 1.02) { ++swap_over_run; swap_under_run = 0; }
            else if (adaptive.average_ms < adaptive.budget_ms * 0.93) { ++swap_under_run; swap_over_run = 0; }
            else swap_over_run = swap_under_run = 0;
            if (swap_auto_interval == 2 && swap_over_run >= 90) swap_auto_interval = 1;
            else if (swap_auto_interval == 1 && swap_under_run >= 120) swap_auto_interval = 2;
        }
        want = swap_auto_interval;
    } else {
        return;
    }
    if (want != current && mac_window_set_swap_interval(want))
        fprintf(stderr, "swap interval: %d (%s)\n", want, setting >= 1 ? "mac.swap_interval" : "30 frames a second on a 60 Hz panel");
}


/* seconds: the time since the previous frame; wait_seconds: how much of it was a wait that is not the
   frame's work: a frame rate limiter's and, with vsync, the swap's wait for the display (mac_window.c) */
void mac_adaptive_detail_note_frame(double seconds, double wait_seconds)
{
    double work = seconds - wait_seconds;
    unsigned skipped;
    int direction = 0, before;
    if (work < 0.0) work = 0.0;
    adaptive_refresh();
    if (!mac_adaptive_detail_wall_mode()) swap_interval_update();
    if (!adaptive_on) return;
    /* with a swap interval the work is under the target by a margin, so that it does not meet a refresh
       it just misses; the rungs come back at MAC_GOV_UP_UNDER of it (the band between is the hysteresis) */
    adaptive.down_over = (!mac_adaptive_detail_wall_mode() && mac_window_swap_interval && mac_window_swap_interval() > 0) ?
        MAC_GOV_VSYNC_DOWN_OVER : MAC_GOV_DOWN_OVER;
    if (adaptive_forced_level() >= 0) {
        if (adaptive.level != adaptive_forced) {
            adaptive.level = adaptive_forced;
            ++adaptive_serial;
        }
        return;
    }
    skipped = adaptive_render_skipped | adaptive_capped_rungs();
    if (!adaptive_resolution_available()) skipped |= 1u << MAC_ADAPT_RESOLUTION;
    before = adaptive.level;
    if (mac_governor_note(&adaptive, work * 1000.0, seconds * 1000.0, skipped, &direction)) {
        static mach_timebase_info_data_t timebase;
        uint64_t now = mach_absolute_time();
        double since;
        ++adaptive_serial;
        if (!timebase.denom) mach_timebase_info(&timebase);
        since = adaptive_last_log ? (double)(now - adaptive_last_log) * timebase.numer / timebase.denom / 1e9 : 1e9;
        if (since >= 1.0) {
            fprintf(stderr, "adaptive detail: level %d to %d (%s %s), average %.1f ms of a target %.1f ms",
                before, adaptive.level, direction < 0 ? "lower:" : "back:",
                adaptive_rung_names[(direction < 0 ? adaptive.level : before) - 1], adaptive.average_ms,
                adaptive.budget_ms);
            if (adaptive_suppressed)
                fprintf(stderr, " (%lu changes in the last second not logged)", adaptive_suppressed);
            fputc('\n', stderr);
            adaptive_last_log = now;
            adaptive_suppressed = 0;
        } else {
            ++adaptive_suppressed;
        }
    }
}

/* a new map starts the ladder again */
void mac_adaptive_detail_new_map(void)
{
    if (adaptive_started) {
        mac_governor_new_map(&adaptive);
        ++adaptive_serial;
    }
}

void mac_adaptive_detail_summary(void)
{
    int level;
    if (!adaptive_started || !adaptive_on || !adaptive.counted) return;
    fprintf(stderr, "adaptive detail summary: target %.1f ms, frames %lu, mean %.2f ms, maximum level %d, final level %d, steps down %lu, up %lu, last 300 frames mean %.2f ms\n",
        adaptive.budget_ms, adaptive.counted, adaptive.total_ms / (double)adaptive.counted, adaptive.maximum_level,
        adaptive.level, adaptive.downs, adaptive.ups, adaptive.last_window_ms);
    if (swap_wait_frames)
        fprintf(stderr, "adaptive detail swap wait: mean %.2f ms over %lu frames (not counted as work)\n",
            swap_wait_total * 1000.0 / (double)swap_wait_frames, swap_wait_frames);
    for (level = 0; level <= MAC_ADAPT_RUNGS; ++level)
        if (adaptive.level_frames[level])
            fprintf(stderr, "adaptive detail level %d: %lu frames, mean %.2f ms%s%s\n", level, adaptive.level_frames[level],
                adaptive.level_ms[level] / (double)adaptive.level_frames[level], level ? ", last rung: " : "",
                level ? adaptive_rung_names[level - 1] : "");
}
