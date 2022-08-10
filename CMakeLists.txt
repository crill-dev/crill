cmake_minimum_required(VERSION 3.22)
project(crill)

include_directories(include)

add_executable(tests tests/main.cpp tests/spin_mutex_test.cpp)
target_compile_features(tests PRIVATE cxx_std_17)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # Avoid "undefined reference to 'pthread_create'" linker error:
    find_package(Threads REQUIRED)
    TARGET_LINK_LIBRARIES(tests PRIVATE Threads::Threads)
endif()

add_test(NAME tests COMMAND tests)
enable_testing()
