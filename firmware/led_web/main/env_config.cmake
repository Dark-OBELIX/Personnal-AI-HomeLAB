# Lit un fichier .env (CLE=valeur) et génère un en-tête C avec des #define CFG_<CLE>.
#
# - lignes vides et commentaires (#) ignorés ;
# - valeurs entre "..." ou '...' : prises telles quelles (espaces, # et ; autorisés) ;
# - valeurs sans guillemets : un commentaire en fin de ligne (" # ...") est retiré ;
# - le fichier .env est surveillé : le modifier relance la configuration au prochain build.
#
# Le chemin peut être remplacé par la variable d'environnement LED_ENV_FILE.

function(led_generate_env_config env_file out_header)
    if(DEFINED ENV{LED_ENV_FILE})
        set(env_file "$ENV{LED_ENV_FILE}")
    endif()
    if(NOT EXISTS "${env_file}")
        message(FATAL_ERROR
            "Configuration introuvable : ${env_file}\n"
            "Copie .env.example en .env et remplis-le (Wi-Fi, bandeau, MQTT).")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${env_file}")

    # Les listes CMake réagissent à ';', '[' et ']' : on les protège avant de découper en lignes.
    file(READ "${env_file}" content)
    string(REPLACE "\r" "" content "${content}")
    string(REPLACE ";" "@LED_SEMI@" content "${content}")
    string(REPLACE "[" "@LED_LBR@" content "${content}")
    string(REPLACE "]" "@LED_RBR@" content "${content}")
    string(REPLACE "\n" ";" lines "${content}")

    foreach(line IN LISTS lines)
        string(REPLACE "@LED_SEMI@" ";" line "${line}")
        string(REPLACE "@LED_LBR@" "[" line "${line}")
        string(REPLACE "@LED_RBR@" "]" line "${line}")
        string(STRIP "${line}" line)
        if("${line}" STREQUAL "" OR "${line}" MATCHES "^#")
            continue()
        endif()
        if(NOT "${line}" MATCHES "^([A-Za-z_][A-Za-z0-9_]*)[ \t]*=(.*)$")
            message(WARNING "Ligne ignorée dans ${env_file} : ${line}")
            continue()
        endif()
        set(key "${CMAKE_MATCH_1}")
        set(val "${CMAKE_MATCH_2}")
        string(STRIP "${val}" val)
        if("${val}" MATCHES "^\"([^\"]*)\"[ \t]*(#.*)?$")
            set(val "${CMAKE_MATCH_1}")
        elseif("${val}" MATCHES "^'([^']*)'[ \t]*(#.*)?$")
            set(val "${CMAKE_MATCH_1}")
        else()
            string(REGEX REPLACE "[ \t]+#.*$" "" val "${val}")
            string(STRIP "${val}" val)
        endif()
        set(LEDENV_${key} "${val}")
    endforeach()

    # Clés attendues
    set(string_keys
        WIFI_SSID WIFI_PASSWORD
        DEVICE_NAME DEVICE_ID DEVICE_HOSTNAME
        LED_MODEL LED_COLOR_ORDER
        MQTT_URI MQTT_USER MQTT_PASSWORD MQTT_BASE_TOPIC HA_DISCOVERY_PREFIX
        OTA_PASSWORD)
    set(int_keys LED_GPIO LED_COUNT LED_MAX_BRIGHTNESS LED_GAMMA DEFAULT_TRANSITION_MS)

    foreach(key WIFI_SSID WIFI_PASSWORD)
        if(NOT DEFINED LEDENV_${key} OR "${LEDENV_${key}}" STREQUAL "")
            message(FATAL_ERROR "${key} est obligatoire dans ${env_file}")
        endif()
    endforeach()
    if(NOT "${LEDENV_DEVICE_ID}" STREQUAL "" AND NOT "${LEDENV_DEVICE_ID}" MATCHES "^[a-z0-9_]+$")
        message(FATAL_ERROR "DEVICE_ID : minuscules, chiffres et _ uniquement (reçu : ${LEDENV_DEVICE_ID})")
    endif()
    if(NOT "${LEDENV_DEVICE_HOSTNAME}" STREQUAL "" AND NOT "${LEDENV_DEVICE_HOSTNAME}" MATCHES "^[a-z0-9-]+$")
        message(FATAL_ERROR "DEVICE_HOSTNAME : minuscules, chiffres et - uniquement (reçu : ${LEDENV_DEVICE_HOSTNAME})")
    endif()

    set(header "/* Généré depuis ${env_file} : ne pas modifier, ne pas versionner. */\n#pragma once\n\n")
    # Valeur vide = valeur par défaut d'app_config.h (MQTT_URI ou OTA_PASSWORD vides : fonction désactivée)
    foreach(key IN LISTS string_keys)
        if(DEFINED LEDENV_${key} AND NOT "${LEDENV_${key}}" STREQUAL "")
            set(v "${LEDENV_${key}}")
            string(REPLACE "\\" "\\\\" v "${v}")
            string(REPLACE "\"" "\\\"" v "${v}")
            string(APPEND header "#define CFG_${key} \"${v}\"\n")
        endif()
    endforeach()
    foreach(key IN LISTS int_keys)
        if(DEFINED LEDENV_${key} AND NOT "${LEDENV_${key}}" STREQUAL "")
            if(NOT "${LEDENV_${key}}" MATCHES "^[0-9]+$")
                message(FATAL_ERROR "${key} doit être un entier positif (reçu : ${LEDENV_${key}})")
            endif()
            string(APPEND header "#define CFG_${key} ${LEDENV_${key}}\n")
        endif()
    endforeach()

    # N'écrit que si le contenu change : évite de tout recompiler à chaque build
    if(EXISTS "${out_header}")
        file(READ "${out_header}" old)
        if("${old}" STREQUAL "${header}")
            return()
        endif()
    endif()
    file(WRITE "${out_header}" "${header}")
endfunction()
