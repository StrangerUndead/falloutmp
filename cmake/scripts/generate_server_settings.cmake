# Usage: "cmake -P generate_server_settings.cmake -DESM_PREFIX=<prefix_here> -DSERVER_SETTINGS_JSON_PATH=<path_to_server_settings.json> -DOFFLINE_MODE=<true_or_false>"

# read current server-settings.json
if(EXISTS "${SERVER_SETTINGS_JSON_PATH}")
    file(READ "${SERVER_SETTINGS_JSON_PATH}" SERVER_SETTINGS_JSON)
else()
    set(SERVER_SETTINGS_JSON "{}")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "dataDir" "\"data\"")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "name" "\"My Server\"")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "game" "\"fallout4\"")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "loadOrder" "[\"${ESM_PREFIX}Fallout4.esm\"]")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "npcEnabled" "false")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "port" "7777")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "maxPlayers" "100")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "npcSettings" "{}")
endif()

if(OFFLINE_MODE)
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "offlineMode" "true")
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "master" "\"\"")
else()
    string(JSON SERVER_SETTINGS_JSON SET "${SERVER_SETTINGS_JSON}" "offlineMode" "false")
endif()

file(WRITE "${SERVER_SETTINGS_JSON_PATH}" "${SERVER_SETTINGS_JSON}")

if(SERVER_SETTINGS_BASE_JSON_PATH)
  file(WRITE "${SERVER_SETTINGS_BASE_JSON_PATH}" "${SERVER_SETTINGS_JSON}")
endif()
