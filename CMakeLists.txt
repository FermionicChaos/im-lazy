cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Utility function for fetching dependencies in one line.
function(fetch_dependency AGIT_REPO AGIT_TAG ANAME)
    # Remaining args are variable=value pairs for cache options
    set(options)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(FETCHDEP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Declare the dependency
    FetchContent_Declare(
        ${ANAME}
        GIT_REPOSITORY ${AGIT_REPO}
        GIT_TAG        ${AGIT_TAG}
        SOURCE_DIR     ${CMAKE_SOURCE_DIR}/dep/${ANAME}-src
    )

    # Apply cache variables
    foreach(arg IN LISTS ARGN)
        string(REPLACE "=" ";" kv "${arg}")
        list(GET kv 0 key)
        list(GET kv 1 val)
        set(${key} ${val} CACHE BOOL "")
    endforeach()

    # Actually fetch it
    FetchContent_MakeAvailable(${ANAME})
endfunction()

include(FetchContent)

# Dependencies go here. (Examples commented out)
# fetch_dependency(https://github.com/FermionicChaos/geodesy-math.git master geodesy-math)
# fetch_dependency(https://github.com/FermionicChaos/geodesy-gpu.git master geodesy-gpu VULKAN_SDK_VERSION=1.4.321.0)
# fetch_dependency(https://github.com/glfw/glfw.git 3.4 glfw BUILD_SHARED_LIBS=OFF GLFW_BUILD_WAYLAND=OFF)

set(PROJECT_NAME "im-lazy")

file(GLOB_RECURSE INC
    "inc/*.h"
)

file(GLOB_RECURSE SRC
    "src/*.h"
    "src/*.cpp"
    "src/*.c"
)

project(${PROJECT_NAME})

add_executable(${PROJECT_NAME} ${INC} ${SRC} main.cpp)

# Compiler Flags & Macros Go here.
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_SYSTEM_NAME}/${CMAKE_BUILD_TYPE}/)

# Set Include Directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc/)
# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dep/geodesy-math-src/inc/)
# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dep/geodesy-gpu-src/inc/)
# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dep/glfw-src/include/)

# Link Libraries Go here.
# target_link_libraries(${PROJECT_NAME} PRIVATE geodesy-gpu)
# target_link_libraries(${PROJECT_NAME} PRIVATE glfw)

