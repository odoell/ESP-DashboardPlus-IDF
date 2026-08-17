# When used directly as a component (cloned into components/ or referenced via
# EXTRA_COMPONENT_DIRS), LIB_DIR defaults to this file's directory.
# When included from an external thin wrapper, the caller sets LIB_DIR first.
if(NOT DEFINED LIB_DIR)
    set(LIB_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

# Generate TLS certificates only in HTTPS mode (idempotent — skips when certs exist)
if(NOT CONFIG_DASHBOARD_USE_HTTP)
    if(EXISTS "${LIB_DIR}/scripts/gen_cert.py")
        execute_process(
            COMMAND ${PYTHON} "${LIB_DIR}/scripts/gen_cert.py" "${LIB_DIR}/certs"
            RESULT_VARIABLE _cert_result
            ERROR_VARIABLE  _cert_error
        )
        if(NOT _cert_result EQUAL 0)
            message(FATAL_ERROR "Certificate generation failed:\n${_cert_error}")
        endif()
    endif()
endif()

# Convert dashboard.html → dashboard_html.c when its source or converter changes.
if(EXISTS "${LIB_DIR}/scripts/html_to_header_idf.py" AND
   NOT CMAKE_SCRIPT_MODE_FILE)
    add_custom_command(
        OUTPUT
                "${LIB_DIR}/src/dashboard_html.c"
                "${LIB_DIR}/include/dashboard_html.h"
        COMMAND ${PYTHON} "${LIB_DIR}/scripts/html_to_header_idf.py"
                "${LIB_DIR}/extras/dashboard.html"
                "${LIB_DIR}/src/dashboard_html.c"
                "${LIB_DIR}/include/dashboard_html.h"
        DEPENDS
                "${LIB_DIR}/extras/dashboard.html"
                "${LIB_DIR}/scripts/html_to_header_idf.py"
        COMMENT "Generating embedded dashboard HTML"
        VERBATIM
    )
    add_custom_target(esp_dashboard_plus_html
        DEPENDS
                "${LIB_DIR}/src/dashboard_html.c"
                "${LIB_DIR}/include/dashboard_html.h"
    )
endif()

set(COMPONENT_SRCS
    "${LIB_DIR}/src/esp_dashboard_plus.cpp"
    "${LIB_DIR}/src/dashboard_server.cpp"
    "${LIB_DIR}/src/dashboard_cards.cpp"
)
if(CONFIG_DASHBOARD_ENABLE_OTA)
    list(APPEND COMPONENT_SRCS "${LIB_DIR}/src/dashboard_ota.cpp")
endif()
if(EXISTS "${LIB_DIR}/scripts/html_to_header_idf.py")
    list(APPEND COMPONENT_SRCS "${LIB_DIR}/src/dashboard_html.c")
endif()

if(CONFIG_DASHBOARD_USE_HTTP)
    set(_server_req "esp_http_server")
else()
    set(_server_req "esp_https_server")
endif()

set(DASHBOARD_COMPONENT_REQUIRES
    ${_server_req}
    espressif__cjson
    esp_wifi
    nvs_flash
    esp_event
    esp_timer
)

idf_component_register(
    SRCS              ${COMPONENT_SRCS}
    INCLUDE_DIRS      "${LIB_DIR}/include"
    PRIV_INCLUDE_DIRS "${LIB_DIR}/src"
    REQUIRES          ${DASHBOARD_COMPONENT_REQUIRES}
)

if(TARGET esp_dashboard_plus_html)
    add_dependencies(${COMPONENT_LIB} esp_dashboard_plus_html)
endif()

if(CONFIG_DASHBOARD_ENABLE_OTA)
    idf_component_optional_requires(PUBLIC app_update)
endif()

if(NOT CONFIG_DASHBOARD_USE_HTTP)
    if(EXISTS "${LIB_DIR}/certs/server.crt" AND
       EXISTS "${LIB_DIR}/certs/server.key")
        target_add_binary_data(${COMPONENT_LIB} "${LIB_DIR}/certs/server.crt" TEXT)
        target_add_binary_data(${COMPONENT_LIB} "${LIB_DIR}/certs/server.key" TEXT)
    endif()
endif()
