#!/bin/sh
# New Game -> Load Level -> The Pillar of Autumn with a saved game on the profile must start the level fresh:
# the saved game is neither taken at the map's start (game time near 0, the player where a first start has it) nor kept.
# (Seen on an iBook: New Game loaded a10 from the latest checkpoint.)
# A first run plays a10 from the menus and saves the game (the pause menu's Quit: HALO_MAC_SAVE_QUIT_AT); a second
# run takes New Game with that save in the profile.
# usage: sh tools/mac_menu_new_game_checks.sh SESSION_BINARY DATA_ROOT UI_MAP OUTDIR [more maps for the retail form]
#   (as mac_menu_b30_checks.sh) Prints "checks, N failures" like the other checks.
set -u
binary=$1; root=$2; ui=$3; out=$4; shift 4
rm -rf "$out"; mkdir -p "$out/save" "$out/cache"
wait8=wait,wait,wait,wait,wait,wait,wait,wait,
long=""; i=0; while [ $i -lt 300 ]; do long="${long}wait,"; i=$((i+1)); done
maps="$*"
cd "$out"
# run NAME FRAMES KEYS EXTRA_ENVIRONMENT_ASSIGNMENT
run() {
    name=$1; frames=$2; keys=$3; extra=$4
    env $extra HALO_MAC_MENU=1 HALO_START_IN_MENU=1 HALO_MAC_RENDER_FIXED_STEP=1 HALO_MAC_RENDER_SEED=1835103336 \
        HALO_MAC_RENDER_FRAMES=$frames HALO_MAC_DATA_ROOT="$root" HALO_MAC_MAP_CACHE="$out/cache" \
        HALO_MAC_SAVE_ROOT="$out/save" HALO_MAC_CONFIG_DIR="$out/cfg" HALO_MAC_RENDER_CAPTURE="$out/$name.ppm" \
        HALO_MAC_CUTSCENE_LOG=100 HALO_MAC_MENU_KEY_FRAMES=4 HALO_MAC_MENU_KEYS="$keys" \
        "$binary" "$ui" $maps --gpu-render > "$out/$name.txt" 2>&1
    return $?
}
checks=0; failures=0
check() { checks=$((checks+1)); if [ "$1" != 1 ]; then failures=$((failures+1)); echo "FAIL: $2"; fi; }
yes() { [ "$1" = 0 ] && echo 1 || echo 0; }
# the first run: no saved game, so Campaign's first item is New Game
run first 900 "${wait8}return,${wait8}return,${wait8}return,${wait8}return,${long}end" HALO_MAC_SAVE_QUIT_AT=400
check $(yes $?) "the first run exits 0"
check $(grep -a -q "menus: the game levels.a10.a10 is started" first.txt && echo 1 || echo 0) "the menus start a10"
check $(grep -a -q "checkpoint: saved the game to the persistent storage" first.txt && echo 1 || echo 0) "the first run saves the game"
saved=$(find "$out/save" -name savegame.bin | head -1)
check $([ -n "$saved" ] && echo 1 || echo 0) "a saved game is in the profile"
# the second run: Continue is Campaign's first item, New Game the second
run second 900 "${wait8}return,${wait8}down,wait,wait,return,${wait8}return,${wait8}return,${long}end" HALO_NONE=1
check $(yes $?) "the second run exits 0"
check $(grep -a -q "menus: the game levels.a10.a10 is started" second.txt && echo 1 || echo 0) "New Game starts a10"
line=$(grep -a "campaign: new map" second.txt | head -1)
echo "$line" | grep -q "not allowed"
check $([ $? = 0 ] && echo 1 || echo 0) "the saved game is not looked for ($line)"
after=$(echo "$line" | sed -n 's/.*game time [-0-9]* -> \([-0-9]*\),.*/\1/p')
check $([ "${after:-9999}" -lt 20 ] && echo 1 || echo 0) "the game time is near 0 (got: $after)"
check $(grep -a -q "saved game loaded" second.txt && echo 0 || echo 1) "no saved game is loaded"
# (a10 opens with a cinematic: the first camera it shows is the level's start)
p1=$(grep -a "^cutscene tick.*cinematic=1" first.txt | head -1 | sed -n 's/.*camera=(\([-0-9.,]*\)).*/\1/p')
p2=$(grep -a "^cutscene tick.*cinematic=1" second.txt | head -1 | sed -n 's/.*camera=(\([-0-9.,]*\)).*/\1/p')
check $([ -n "$p2" ] && [ "$p1" = "$p2" ] && echo 1 || echo 0) "the level starts where a first start has it (camera $p1 / $p2)"
check $(grep -a -q "menus: the saved game is discarded for a new game" second.txt && echo 1 || echo 0) "the old saved game is discarded"
echo "menu new game: $checks checks, $failures failures"
[ $failures = 0 ]
