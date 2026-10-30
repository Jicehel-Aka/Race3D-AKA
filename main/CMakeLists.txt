idf_component_register(
    SRCS "main.cpp"
         "game/game.cpp" "game/race.cpp" "game/render.cpp" "game/track.cpp" "game/tracks_data.cpp"
         "game/sprites.cpp" "game/gfx.cpp" "game/synth.cpp" "game/music.cpp"
    INCLUDE_DIRS "." "game"
    REQUIRES gamebuino aka_runtime
    PRIV_REQUIRES app_update esp_timer)

# The renderer is pure number crunching: always build it optimised, even when
# the project is configured with -Og.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-error=unused-variable -Wno-error=unused-function)
