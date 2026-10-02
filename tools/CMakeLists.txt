# A little command line tool for poking at the engine without the window
add_executable(ve-cli ve_cli.cpp)
target_link_libraries(ve-cli PRIVATE ve_engine Qt6::Gui)
