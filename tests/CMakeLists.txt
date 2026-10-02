qt_add_executable(timeline_test timeline_test.cpp)
target_link_libraries(timeline_test PRIVATE editor_ui Qt6::Test)

enable_testing()
add_test(NAME timeline COMMAND timeline_test)
set_tests_properties(timeline PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
