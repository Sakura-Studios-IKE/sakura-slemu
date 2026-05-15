cmake_minimum_required(VERSION 3.10)
project(slemu C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB SLEMU_SRC src/*.c)
add_executable(slemu ${SLEMU_SRC})

if(MSVC)
    target_compile_options(slemu PRIVATE /W3 /wd4244 /wd4267 /wd4996 /wd4100)
else()
    target_compile_options(slemu PRIVATE -Wall -Wextra
        -Wno-unused-parameter -Wno-unused-variable
        -Wno-unused-function -Wno-unused-but-set-variable)
    target_link_libraries(slemu PRIVATE m)
endif()

install(TARGETS slemu RUNTIME DESTINATION bin)
