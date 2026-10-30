cmake_minimum_required(VERSION 3.19)
project(qt_smoke LANGUAGES CXX)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Widgets SerialPort)

qt_standard_project_setup()

qt_add_executable(qt_smoke
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    bms_log_parser.h
    bms_log_parser.cpp
)

target_compile_features(qt_smoke PRIVATE cxx_std_17)

target_link_libraries(qt_smoke
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::SerialPort
)

install(TARGETS qt_smoke
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET qt_smoke
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
