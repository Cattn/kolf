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
ecm_add_test(objectregistrytest.cpp
    TEST_NAME objectregistrytest
    LINK_LIBRARIES kolfprivate Qt6::Test)
target_include_directories(objectregistrytest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(objectregistrytest PRIVATE KOLF_OBJECT_CATALOG="${CMAKE_SOURCE_DIR}/protocol/object-catalog.json"
    KOLF_SOURCE_OBJECTS="${CMAKE_SOURCE_DIR}/objects")
set_tests_properties(objectregistrytest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
ecm_add_test(dataobjecttest.cpp
    TEST_NAME dataobjecttest
    LINK_LIBRARIES kolfprivate Qt6::Test)
target_include_directories(dataobjecttest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(dataobjecttest PRIVATE KOLF_SOURCE_OBJECTS="${CMAKE_SOURCE_DIR}/objects")
set_tests_properties(dataobjecttest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
