idf_component_register(
    SRCS "main.cpp"
         "game/game.cpp" "game/car_art.cpp" "game/race.cpp" "game/render.cpp" "game/track.cpp" "game/tracks_data.cpp"
         "game/sprites.cpp" "game/gfx.cpp" "game/synth.cpp" "game/music.cpp"
         "game/ws.cpp" "game/online.cpp" "game/i18n.cpp" "game/car_shadow_art.cpp" "game/tech_art.cpp" "game/scen_art.cpp" "net_esp.cpp"
    INCLUDE_DIRS "." "game"
    REQUIRES gamebuino aka_runtime
    PRIV_REQUIRES app_update esp_timer esp_wifi esp_netif esp_event esp-tls mbedtls lwip esp_system)

# The renderer is pure number crunching: always build it optimised, even when
# the project is configured with -Og.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-error=unused-variable -Wno-error=unused-function)
