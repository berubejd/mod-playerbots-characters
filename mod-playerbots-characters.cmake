if(TARGET modules)
    # Optional mod_weather_vibe integration
    if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod_weather_vibe/src/core/mod_wv_core.h")
        target_compile_definitions(modules PRIVATE MOD_WEATHER_VIBE)
        target_include_directories(modules PRIVATE
            "${CMAKE_SOURCE_DIR}/modules/mod_weather_vibe/src/core")
        message(STATUS "[mod-playerbots-characters] mod_weather_vibe found - weather integration enabled")
    else()
        message(STATUS "[mod-playerbots-characters] mod_weather_vibe not found - weather integration disabled")
    endif()

    # Include nlohmann/json library
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/deps/nlohmann/json.hpp")
        target_include_directories(modules PRIVATE ${CMAKE_CURRENT_LIST_DIR}/deps)
        message(STATUS "[mod-playerbots-characters] Using bundled nlohmann/json")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/nlohmann")
        target_include_directories(modules PRIVATE ${CMAKE_SOURCE_DIR}/deps/nlohmann)
        message(STATUS "[mod-playerbots-characters] Using AzerothCore deps nlohmann/json")
    else()
        find_package(nlohmann_json CONFIG QUIET)
        if(nlohmann_json_FOUND)
            target_link_libraries(modules PRIVATE nlohmann_json::nlohmann_json)
            message(STATUS "[mod-playerbots-characters] Using system nlohmann/json")
        else()
            message(FATAL_ERROR "[mod-playerbots-characters] nlohmann/json not found.\n"
                              "  Please place json.hpp in deps/nlohmann/json.hpp\n"
                              "  or install via: apt install nlohmann-json3-dev")
        endif()
    endif()

    # Include fmt library
    if(TARGET fmt)
        target_link_libraries(modules PRIVATE fmt)
        message(STATUS "[mod-playerbots-characters] Using AzerothCore fmt library")
    else()
        find_package(fmt CONFIG QUIET)
        if(fmt_FOUND)
            target_link_libraries(modules PRIVATE fmt::fmt)
            message(STATUS "[mod-playerbots-characters] Using system fmt library")
        else()
            message(FATAL_ERROR "[mod-playerbots-characters] fmt library not found.\n"
                              "  Ubuntu/Debian: sudo apt install libfmt-dev\n"
                              "  Or build AzerothCore with full deps")
        endif()
    endif()

    # Include cpp-httplib (header-only, must be placed in src/httplib.h)
    # Copy httplib.h from mod-ollama-chat/src/httplib.h or download from:
    # https://github.com/yhirose/cpp-httplib
    target_include_directories(modules PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

    # Enable SSL/TLS support for HTTPS connections
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND OR OPENSSL_FOUND)
        target_compile_definitions(modules PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
        target_link_libraries(modules PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        message(STATUS "[mod-playerbots-characters] OpenSSL found - HTTPS support enabled")
    else()
        message(WARNING "[mod-playerbots-characters] OpenSSL not found - only HTTP (no HTTPS) available")
        if(WIN32)
            message(STATUS "[mod-playerbots-characters] Windows: vcpkg install openssl")
        else()
            message(STATUS "[mod-playerbots-characters] Linux: apt install libssl-dev")
        endif()
    endif()

    # Platform-specific threading and networking
    if(WIN32)
        target_link_libraries(modules PRIVATE ws2_32 crypt32)
    else()
        target_link_libraries(modules PRIVATE pthread)
    endif()
endif()
