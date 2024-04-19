# components-misc

## Components for my embedded projects

A bag of mixed compoments used in my embedded projects.

APIs are not stable. When a component matures later, it may move out into its own repo.


#### Documentation
   *  View [API documentation](https://zwiebert.github.io/components-misc) 
   *  Create API documentation `make doxy-api-view`
   *  Crate source documentation `make doxy-dev-view`

#### Testing  
   *  `make host-test-all`  configure, build and runs all tests

#### Build

#####  Build in ESP-IDF Project
   * The component(s) are located in subdirectory `./components`
   * The required external components are located in subdirectory `./external`
   * Add components directories to list `EXTRA_COMPONENTS_DIR`, so they can be found by idf
       * `file(GLOB extra_dirs external/*/components external/*/external/*/components external/*/external/*/external/*/components)`      
       * `list(APPEND EXTRA_COMPONENTS_DIR ${extra_dirs})`
   * Add ia compoment name to PRIV_REQUIRE or REQUIRE in CMakeLists.txt to use it

##### Build in CMake Project
 * To add component xyz from this lib:
    * source-CMakeLists.txt: `target_link_libraries(main PUBLIC ... xyz ...)`
    * project-CMakeLists.txt: `add_subdirectory(external/components-misc/components/xyz)`

##### Build in IDE like STM32CubeIde
 * add include path: `external/components-misc/components/xyz/include`
 * add source path: `external/components-misc/components/xyz/src`
    *  if there is no src subdirectory yet, you have to add the root directory and filter out any unwanted subdirectories


#### Online Git Repositories
   * This repository on [GitHub](https://github.com/zwiebert/components-misc.git)
   * ESP-IDF Project which uses this lib: [tronferno-mcu](https://github.com/zwiebert/tronferno-mcu.git)
   * ESP-IDF+STM32 Project which uses this lib: [rv garden irrigation](https://github.com/zwiebert/irrigation_control.git)

