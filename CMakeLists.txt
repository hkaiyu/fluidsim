cmake_minimum_required(VERSION 3.25)
project(fluidsim LANGUAGES CXX)

find_package(Vulkan REQUIRED)

include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)

# Unity build
add_executable(fluidsim src/main.cpp)

set_target_properties(fluidsim PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# this is just to get rid of msvc warnings
if (MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
endif()

target_compile_options(fluidsim PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc- /GR->
)

target_link_libraries(fluidsim PRIVATE
    glfw
    Vulkan::Headers
)

target_include_directories(fluidsim PRIVATE
    src/third_party
    src/vulkan/generated
)

target_compile_definitions(fluidsim PRIVATE
    VK_NO_PROTOTYPES
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_IMPLEMENTATION
    VOLK_IMPLEMENTATION
    GLFW_INCLUDE_NONE
    STB_IMAGE_IMPLEMENTATION
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

