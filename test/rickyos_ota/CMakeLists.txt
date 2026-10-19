cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rickyos_ota_tests CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  enable_testing()
endif()

find_package(OpenSSL REQUIRED)
add_executable(RickyOtaTest
  RickyOtaTest.cpp
  ${REPO_ROOT}/src/network/OtaUpdater.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
)
target_include_directories(RickyOtaTest PRIVATE
  stubs
  ${REPO_ROOT}
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
)
target_compile_definitions(RickyOtaTest PRIVATE RICKYOS_PRODUCT=1 CROSSPOINT_VERSION="1.1.0-dev")
target_link_libraries(RickyOtaTest PRIVATE OpenSSL::Crypto)
target_compile_options(RickyOtaTest PRIVATE -UNDEBUG)
add_test(NAME RickyOtaTest COMMAND RickyOtaTest)

add_executable(HttpDownloaderTrustTest
  HttpDownloaderTrustTest.cpp
  ${REPO_ROOT}/src/network/HttpDownloader.cpp
)
target_include_directories(HttpDownloaderTrustTest PRIVATE stubs ${REPO_ROOT} ${REPO_ROOT}/lib/Logging)
target_compile_definitions(HttpDownloaderTrustTest PRIVATE RICKYOS_PRODUCT=1 CROSSPOINT_VERSION="1.1.0-dev")
target_compile_options(HttpDownloaderTrustTest PRIVATE -UNDEBUG)
add_test(NAME HttpDownloaderTrustTest COMMAND HttpDownloaderTrustTest)

# Run the same parser, updater and Flash-failure checks with the A4 identity/routes.
add_executable(RickyOtaA4Test
  RickyOtaTest.cpp
  ${REPO_ROOT}/src/network/OtaUpdater.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
)
target_include_directories(RickyOtaA4Test PRIVATE
  stubs
  ${REPO_ROOT}
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
)
target_compile_definitions(RickyOtaA4Test PRIVATE RICKYOS_PRODUCT=1 FREEINK_DEVICE_EEGO_A4=1 CROSSPOINT_VERSION="1.1.0-dev")
target_link_libraries(RickyOtaA4Test PRIVATE OpenSSL::Crypto)
target_compile_options(RickyOtaA4Test PRIVATE -UNDEBUG)
add_test(NAME RickyOtaA4Test COMMAND RickyOtaA4Test)
