idf_component_register(
    SRCS "main.c" "led_ctrl.c" "net.c" "web.c" "mqtt_ha.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "index.html")

# Configuration lue dans ../.env et transformée en en-tête C (env_config.h).
# Pas pendant la phase d'analyse des dépendances de l'ESP-IDF (mode script).
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include("${CMAKE_CURRENT_LIST_DIR}/env_config.cmake")
    led_generate_env_config("${CMAKE_CURRENT_LIST_DIR}/../.env"
                            "${CMAKE_CURRENT_BINARY_DIR}/generated/env_config.h")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()
