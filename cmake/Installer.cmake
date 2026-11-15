# Локальная упаковка. Обычная сборка не требует Inno Setup.
find_program(OldMoneyTatarin_ISCC_EXECUTABLE NAMES ISCC.exe ISCC
    HINTS
        "[HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Inno Setup 6_is1;InstallLocation]"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Inno Setup 6_is1;InstallLocation]"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Inno Setup 6_is1;InstallLocation]"
        "C:/Program Files (x86)/Inno Setup 6"
        "C:/Program Files/Inno Setup 6"
    DOC "Локальный компилятор Inno Setup 6 (ISCC.exe)")

if(NOT OldMoneyTatarin_ISCC_EXECUTABLE)
    message(STATUS "Inno Setup не найден; цель installer недоступна. Укажите OldMoneyTatarin_ISCC_EXECUTABLE")
    return()
endif()

function(oldmoneytatarin_installer)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(MSVC AND NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(FATAL_ERROR "Не найдены распространяемые библиотеки MSVC для установщика")
    endif()
    set(INSTALLER_RUNTIME_LIBRARIES "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
    set(INSTALLER_STAGE "${CMAKE_BINARY_DIR}/installer/$<CONFIG>/app")
    set(INSTALLER_OUTPUT "${CMAKE_SOURCE_DIR}/dist/installer")
    configure_file("${CMAKE_SOURCE_DIR}/installer/windows.iss.in"
                   "${CMAKE_BINARY_DIR}/installer/windows.iss.in" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/installer/$<CONFIG>/windows.iss"
         INPUT "${CMAKE_BINARY_DIR}/installer/windows.iss.in")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/PackageInstaller.cmake.in"
                   "${CMAKE_BINARY_DIR}/installer/package.cmake.in" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/installer/$<CONFIG>/package.cmake"
         INPUT "${CMAKE_BINARY_DIR}/installer/package.cmake.in")
    add_custom_target(installer
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/installer/$<CONFIG>/package.cmake"
        DEPENDS ${Launcher_Name} "${Launcher_Name}_filelink" NewLaunch NewLaunchLegacy JavaCheck
        USES_TERMINAL
        COMMENT "Сборка локального установщика OldMoneyTatarin через Inno Setup"
        VERBATIM)
    message(STATUS "Inno Setup: ${OldMoneyTatarin_ISCC_EXECUTABLE}")
endfunction()

oldmoneytatarin_installer()
