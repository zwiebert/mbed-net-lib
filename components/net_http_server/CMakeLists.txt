set(srcs  src/http_server.cc)
set(platform_srcs esp32/http_server.cc esp32/uri_handlers.cc) 

if(CONFIG_HTTPD_WS_SUPPORT)
   list(APPEND platform_srcs esp32/websocket.cc)
else()
    message(STATUS "http_server: WebSocket is disabled in esp_http_server component")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../source_filter.cmake")

if(COMMAND idf_component_register)
idf_component_register(
    SRCS ${srcs} 
    INCLUDE_DIRS "include" 
    PRIV_INCLUDE_DIRS "src" 
    REQUIRES   esp_http_server
    PRIV_REQUIRES  cli utils_debug utils_misc uout
                    esp_wifi mbedtls #PRIV_ESP_IDF
 )

 
target_compile_options(${COMPONENT_LIB} PRIVATE "SHELL:-include sdkconfig.h")

else()
add_library(net_http_server STATIC ${srcs})
target_include_directories(net_http_server PUBLIC include PRIVATE src)
target_link_libraries(net_http_server PUBLIC esp_http_server PRIVATE cli utils_debug uout)
endif()
