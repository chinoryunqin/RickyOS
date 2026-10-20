find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GUIDE_HEADER ${REPO_ROOT}/src/util/UserGuide.generated.h)
add_custom_command(OUTPUT ${GUIDE_HEADER}
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/generate_userguide_epub.py --bundled
  DEPENDS ${REPO_ROOT}/scripts/generate_userguide_epub.py
          ${REPO_ROOT}/docs/user-guide/en.xhtml ${REPO_ROOT}/docs/user-guide/zh-CN.xhtml
          ${REPO_ROOT}/docs/user-guide/style.css
)
add_executable(UserGuideTest UserGuideTest.cpp ${REPO_ROOT}/src/util/UserGuide.cpp ${GUIDE_HEADER})
target_include_directories(UserGuideTest BEFORE PRIVATE stubs ${REPO_ROOT}/src/util)
target_link_libraries(UserGuideTest PRIVATE crosspoint_test_common GTest::gtest_main)
# Exercise the preserved upstream implementation separately from the product.
target_compile_options(UserGuideTest PRIVATE -URICKYOS_PRODUCT)
gtest_discover_tests(UserGuideTest)
add_executable(ProductUserGuideTest ProductUserGuideTest.cpp ${REPO_ROOT}/src/util/UserGuide.cpp)
target_include_directories(ProductUserGuideTest BEFORE PRIVATE stubs ${REPO_ROOT}/src/util)
target_link_libraries(ProductUserGuideTest PRIVATE crosspoint_test_common)
add_test(NAME ProductUserGuideTest COMMAND ProductUserGuideTest)
add_test(NAME UserGuideEpub
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_userguide_epub.py)
