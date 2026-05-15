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

component_compile_options(${comp_compile_opts})
component_compile_features(${comp_compile_feats})
