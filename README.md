## mbed-net-lib

### Components for my embedded projects

Some compoments for networking.

#### Documentation
   *  View [API documentation](https://zwiebert.github.io/mbed-net-lib)
   *  Create and view API documentation `make doxy-api-view`
   *  Crate and view source documentation `make doxy-dev-view`

#### Testing
   * work in progress
   * for now tests are run from  application or a separate test package. Using git submodule to get all dependencies was not feasable.

#### Build

#####  Build in ESP-IDF Project
   * The component(s) are located in subdirectory `./components`
   * The required external components are located in subdirectory `./external`
   * Add components directories to list `EXTRA_COMPONENTS_DIR`, so they can be found by idf
       * `file(GLOB extra_dirs external/*/components external/*/external/*/components external/*/external/*/external/*/components)`      
       * `list(APPEND EXTRA_COMPONENTS_DIR ${extra_dirs})`
   * Add a compoment name to PRIV_REQUIRE or REQUIRE in CMakeLists.txt to use it

##### Build in CMake Project
 * To add component xyz from this lib:
    * source-CMakeLists.txt: `target_link_libraries(main PUBLIC ... xyz ...)`
    * project-CMakeLists.txt: `add_subdirectory(external/components-misc/components/xyz)`

##### Build in IDE like STM32CubeIde
 * add include path: `external/components-misc/components/xyz/include`
 * add source path: `external/components-misc/components/xyz/src`
    *  if there is no src subdirectory yet, you have to add the root directory and filter out any unwanted subdirectories


#### Online Git Repositories
   * This repository on [GitHub](https://github.com/zwiebert/mbed-net-lib.git)
   * ESP-IDF Project which uses this lib: [tronferno-mcu](https://github.com/zwiebert/tronferno-mcu.git)
   * ESP-IDF+STM32 Project which uses this lib: [rv garden irrigation](https://github.com/zwiebert/irrigation_control.git)

