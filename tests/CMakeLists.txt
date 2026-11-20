add_executable(xtone_tests
    TestRateMath.cpp
    TestClock.cpp
    TestEngine.cpp
    TestMotion.cpp
    TestSlices.cpp)
target_link_libraries(xtone_tests PRIVATE xtone_dsp Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(xtone_tests)
