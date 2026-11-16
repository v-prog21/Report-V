cmake_minimum_required(VERSION 3.16)
project(SupportIL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql Concurrent)

add_executable(SupportIL
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/reportmanager.cpp
    src/reportmanager.h
    src/reportmodel.cpp
    src/reportmodel.h
    src/settingsmanager.cpp
    src/settingsmanager.h
    src/deviceutils.cpp
    src/deviceutils.h
    src/devicerepository.cpp
    src/devicerepository.h
    src/devicemodel.cpp
    src/devicemodel.h
    src/devicedetaildialog.cpp
    src/devicedetaildialog.h
    src/fgisclient.cpp
    src/fgisclient.h
    src/deviceswidget.cpp
    src/deviceswidget.h
    src/networkmanager.cpp
    src/networkmanager.h
    src/fgissearchdialog.cpp
    src/fgissearchdialog.h
    resources/resources.qrc
)

target_link_libraries(SupportIL PRIVATE Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Concurrent)

option(SUPPORTIL_BUILD_TESTS "Build Support IL self-tests" ON)

if(WIN32)
    set_target_properties(SupportIL PROPERTIES WIN32_EXECUTABLE TRUE)

    # Иконка .exe собирается через windres.
    find_program(SUPPORTIL_WINDRES windres HINTS "C:/Users/Win10_Game_OS/Tools/mingw1310_64/bin")
    if(SUPPORTIL_WINDRES)
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/supportil_icon.obj
            COMMAND "${SUPPORTIL_WINDRES}"
                    -i app.rc
                    -o "${CMAKE_BINARY_DIR}/supportil_icon.obj"
                    --include-dir=.
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/resources"
            DEPENDS resources/app.rc resources/ico.ico
            COMMENT "Embedding application icon"
            VERBATIM)
        target_sources(SupportIL PRIVATE ${CMAKE_BINARY_DIR}/supportil_icon.obj)
    endif()

    # ------------------------------------------------------------------
    #  Развёртывание: Qt-плагины и runtime MinGW рядом с .exe.
    #  Раньше libgcc_s_seh-1.dll, libstdc++-6.dll и libwinpthread-1.dll
    #  приходилось докладывать вручную — теперь они копируются сборкой.
    # ------------------------------------------------------------------
    get_target_property(SUPPORTIL_QMAKE Qt6::qmake IMPORTED_LOCATION)
    if(SUPPORTIL_QMAKE)
        get_filename_component(SUPPORTIL_QT_BIN "${SUPPORTIL_QMAKE}" DIRECTORY)
        find_program(SUPPORTIL_WINDEPLOYQT windeployqt HINTS "${SUPPORTIL_QT_BIN}")
        if(SUPPORTIL_WINDEPLOYQT)
            add_custom_command(TARGET SupportIL POST_BUILD
                COMMAND "${SUPPORTIL_WINDEPLOYQT}"
                        --no-translations
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        --compiler-runtime
                        "$<TARGET_FILE:SupportIL>"
                COMMENT "Deploying Qt runtime"
                VERBATIM)
        endif()
    endif()

    get_filename_component(SUPPORTIL_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(SUPPORTIL_DLL
            libgcc_s_seh-1.dll
            libstdc++-6.dll
            libwinpthread-1.dll)
        if(EXISTS "${SUPPORTIL_MINGW_BIN}/${SUPPORTIL_DLL}")
            add_custom_command(TARGET SupportIL POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${SUPPORTIL_MINGW_BIN}/${SUPPORTIL_DLL}"
                        "$<TARGET_FILE_DIR:SupportIL>/${SUPPORTIL_DLL}"
                COMMENT "Copying ${SUPPORTIL_DLL}"
                VERBATIM)
        endif()
    endforeach()
endif()

if(SUPPORTIL_BUILD_TESTS)
    enable_testing()

    add_executable(supportil_selftest
        tests/selftest.cpp
        src/reportmanager.cpp
        src/reportmanager.h
        src/networkmanager.cpp
        src/networkmanager.h
        src/settingsmanager.cpp
        src/settingsmanager.h
    )
    target_include_directories(supportil_selftest PRIVATE src)
    target_link_libraries(supportil_selftest PRIVATE Qt6::Core Qt6::Network Qt6::Sql)
    add_test(NAME supportil_selftest COMMAND supportil_selftest)
endif()