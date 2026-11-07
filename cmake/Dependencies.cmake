include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# 所有依赖固定到具体 tag。
FetchContent_Declare(googletest
  URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.14.0
  URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7)
FetchContent_Declare(yaml-cpp
  URL https://codeload.github.com/jbeder/yaml-cpp/tar.gz/refs/tags/0.8.0
  URL_HASH SHA256=fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16)
FetchContent_Declare(nlohmann_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
  URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406)
FetchContent_Declare(picosha2
  URL https://codeload.github.com/okdshin/PicoSHA2/tar.gz/refs/tags/v1.0.1
  URL_HASH SHA256=9983136544234e573fe07cc1a22fdf978ad7979043e7be2e9082f6d4991ff8a8)

# yaml-cpp 0.8.0 声明的最低版本过旧，新版 CMake 需要此变量。
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest yaml-cpp nlohmann_json picosha2)

if(VISION_WITH_OPENCV)
  find_package(OpenCV 4 QUIET COMPONENTS core imgproc imgcodecs videoio)
  if(OpenCV_FOUND)
    set(VISION_OPENCV_TARGETS ${OpenCV_LIBS})
  else()
    message(STATUS "未找到系统 OpenCV，回退到 FetchContent 构建 4.x（仅 core/imgproc/imgcodecs/videoio）")
    FetchContent_Declare(opencv
      GIT_REPOSITORY https://github.com/opencv/opencv.git
      GIT_TAG 4.10.0)
    set(BUILD_LIST "core,imgproc,imgcodecs,videoio" CACHE STRING "" FORCE)
    foreach(opt BUILD_TESTS BUILD_PERF_TESTS BUILD_EXAMPLES BUILD_DOCS
                BUILD_opencv_apps BUILD_JAVA BUILD_opencv_python3 BUILD_SHARED_LIBS)
      set(${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_MakeAvailable(opencv)
    set(VISION_OPENCV_TARGETS opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio)
    foreach(t core imgproc imgcodecs videoio)
      target_include_directories(opencv_${t} PUBLIC
        ${OPENCV_MODULE_opencv_${t}_LOCATION}/include)
    endforeach()
  endif()
endif()
