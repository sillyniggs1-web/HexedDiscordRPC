cmake_minimum_required(VERSION 3.18)
project(HexedDiscordRPC CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DISCORD_DIR ${CMAKE_SOURCE_DIR}/third_party/discord)

add_library(discord_partner_sdk SHARED IMPORTED)
set_target_properties(discord_partner_sdk PROPERTIES
    IMPORTED_LOCATION ${DISCORD_DIR}/lib/${ANDROID_ABI}/libdiscord_partner_sdk.so)

add_library(HexedDiscordRPC SHARED src/hexed_discord_rpc.cpp)
target_include_directories(HexedDiscordRPC PRIVATE ${DISCORD_DIR}/include)
target_compile_options(HexedDiscordRPC PRIVATE -fvisibility=hidden -O2)
target_link_libraries(HexedDiscordRPC PRIVATE discord_partner_sdk log)
