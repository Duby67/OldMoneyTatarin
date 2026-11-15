# Explicit local signing targets; ordinary builds remain unsigned.
set(OldMoneyTatarin_SIGNING_THUMBPRINT_FILE "${CMAKE_SOURCE_DIR}/secrets/codesigning/thumbprint.txt"
    CACHE FILEPATH "Identifier of a code signing certificate in CurrentUser/My")
set(OldMoneyTatarin_SIGNING_TIMESTAMP_URL "" CACHE STRING "Optional HTTPS RFC 3161 timestamp server")
find_program(OldMoneyTatarin_POWERSHELL_EXECUTABLE NAMES powershell.exe)
set(OMT_SIGN_TIMESTAMP_ARGS)
if(OldMoneyTatarin_SIGNING_TIMESTAMP_URL)
    list(APPEND OMT_SIGN_TIMESTAMP_ARGS -TimestampUrl "${OldMoneyTatarin_SIGNING_TIMESTAMP_URL}")
endif()
if(WIN32 AND OldMoneyTatarin_POWERSHELL_EXECUTABLE)
    add_custom_target(sign-launcher
        COMMAND "${OldMoneyTatarin_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/sign-windows.ps1"
            -File "$<TARGET_FILE:${Launcher_Name}>"
            -ThumbprintFile "${OldMoneyTatarin_SIGNING_THUMBPRINT_FILE}"
            ${OMT_SIGN_TIMESTAMP_ARGS}
        COMMAND "${OldMoneyTatarin_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/sign-windows.ps1"
            -File "$<TARGET_FILE:${Launcher_Name}_filelink>"
            -ThumbprintFile "${OldMoneyTatarin_SIGNING_THUMBPRINT_FILE}"
            ${OMT_SIGN_TIMESTAMP_ARGS}
        DEPENDS ${Launcher_Name} "${Launcher_Name}_filelink"
        USES_TERMINAL VERBATIM
        COMMENT "Локальная подпись приложения TATARIN")
endif()
