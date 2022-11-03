cmake_minimum_required(VERSION 3.22)
project(crill)

include_directories(include)

add_executable(tests tests/main.cpp tests/spin_mutex_test.cpp tests/reclaim_object_test.cpp tests/tests.h include/crill/atomic_unique_ptr.h tests/atomic_unique_ptr_test.cpp include/crill/spin_on_write_object.h include/crill/reclaim_on_write_object.h)
target_compile_features(tests PRIVATE cxx_std_17)

# Avoid "undefined reference to 'pthread_create'" linker error on Linux
find_package(Threads)
target_link_libraries(tests PRIVATE Threads::Threads)

add_test(NAME tests COMMAND tests)
enable_testing()
