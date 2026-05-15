set(srcs  src/mqtt.cc) 
set(platform_srcs  esp32/mqtt.cc host/mqtt.cc) 
include("${CMAKE_CURRENT_LIST_DIR}/../../source_filter.cmake")

idf_component_register(
    SRCS ${srcs} 
    INCLUDE_DIRS "./include" 
    REQUIRES 
    PRIV_REQUIRES cli uout
                  mqtt lwip #PRIV_ESP_IDF
 )

 
target_compile_options(${COMPONENT_LIB} PRIVATE "SHELL:-include sdkconfig.h" -Wno-error=missing-field-initializers)
