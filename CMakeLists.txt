cmake_minimum_required(VERSION 3.22)
project(crill)

include_directories(include)

option(ENABLE_ASAN "Enable address sanitizer" OFF)
option(ENABLE_UBSAN "Enable UB sanitizer" OFF)
option(ENABLE_THREADSAN "Enable thread sanitizer" OFF)

if(ENABLE_ASAN)
    if (NOT MSVC)
        message("Enabling address sanitizer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
        if(NOT APPLE)
            # AppleClang doesn't have lsan
            # https://developer.apple.com/documentation/code_diagnostics
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
        endif()
    endif()
endif()

if(ENABLE_UBSAN)
    if (NOT MSVC)
        message("Enabling UB sanitizer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=undefined")
    endif()
endif()

if(ENABLE_THREADSAN)
    if (NOT MSVC)
        message("Enabling thread sanitizer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=thread")
    endif()
endif()

add_executable(tests
    tests/main.cpp
    tests/spin_mutex_test.cpp
    tests/reclaim_object_test.cpp
    tests/reclaim_on_write_object_test.cpp
    tests/atomic_unique_ptr_test.cpp
    tests/utility_test.cpp)

target_compile_features(tests PRIVATE cxx_std_17)

# Avoid "undefined reference to 'pthread_create'" linker error on Linux
find_package(Threads)
target_link_libraries(tests PRIVATE Threads::Threads)

add_test(NAME tests COMMAND tests)
enable_testing()
