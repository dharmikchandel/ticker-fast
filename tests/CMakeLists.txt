# Provide GoogleTest discovery helper function (gtest_discover_tests)
include(GoogleTest)

# Build the test_protocol test executable from test_protocol.cpp
add_executable(test_protocol test_protocol.cpp)

# Link the core headers, strict compiler warning flags, and GoogleTest main runner
target_link_libraries(test_protocol PRIVATE
    ticker_fast_core
    ticker_fast_warnings
    GTest::gtest_main
)

# Automatically discover individual GoogleTest test cases and register them with CTest
gtest_discover_tests(test_protocol)

# Build the test_ring_buffer test executable from test_ring_buffer.cpp
add_executable(test_ring_buffer test_ring_buffer.cpp)

# Link the core headers, strict compiler warning flags, and GoogleTest main runner
target_link_libraries(test_ring_buffer PRIVATE
    ticker_fast_core
    ticker_fast_warnings
    GTest::gtest_main
)

# Automatically discover individual GoogleTest test cases and register them with CTest
gtest_discover_tests(test_ring_buffer)

# Build the test_ring_stress test executable from test_ring_stress.cpp
add_executable(test_ring_stress test_ring_stress.cpp)

# Link the core headers, strict compiler warning flags, Threads, and GoogleTest main runner
target_link_libraries(test_ring_stress PRIVATE
    ticker_fast_core
    ticker_fast_warnings
    Threads::Threads
    GTest::gtest_main
)

# Automatically discover individual GoogleTest test cases and register them with CTest
gtest_discover_tests(test_ring_stress)

# Build the test_shm_segment test executable from test_shm_segment.cpp
add_executable(test_shm_segment test_shm_segment.cpp)

# Link the core library (now compiled, contains ShmSegment), strict warnings, and GoogleTest
target_link_libraries(test_shm_segment PRIVATE
    ticker_fast_core
    ticker_fast_warnings
    GTest::gtest_main
)

# Automatically discover individual GoogleTest test cases and register them with CTest
gtest_discover_tests(test_shm_segment)
