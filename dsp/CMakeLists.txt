add_library(xtone_dsp STATIC
    RateMath.cpp
    HitVoice.cpp
    Lane.cpp
    SliceModel.cpp
    OnsetDetector.cpp
    DefaultKick.cpp
    Engine.cpp)
target_include_directories(xtone_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(xtone_dsp PUBLIC cxx_std_20)
set_target_properties(xtone_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    target_compile_options(xtone_dsp PRIVATE /W4 /permissive- /fp:precise)
else()
    target_compile_options(xtone_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()
