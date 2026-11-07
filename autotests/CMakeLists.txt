find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Test)
include(ECMAddTests)
ecm_add_test(resultsummarytest.cpp
    TEST_NAME resultsummarytest
    LINK_LIBRARIES kolfprivate Qt6::Test)
target_include_directories(resultsummarytest PRIVATE ${CMAKE_SOURCE_DIR}/src)
ecm_add_test(serverlisttest.cpp
    TEST_NAME serverlisttest
    LINK_LIBRARIES kolfprivate Qt6::Test)
target_include_directories(serverlisttest PRIVATE ${CMAKE_SOURCE_DIR}/src)
ecm_add_test(invitelinktest.cpp
    TEST_NAME invitelinktest
    LINK_LIBRARIES kolfprivate Qt6::Test)
target_include_directories(invitelinktest PRIVATE ${CMAKE_SOURCE_DIR}/src)
