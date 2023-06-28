cmake_minimum_required(VERSION 3.10) # lowest version this was tested with

project("purple-signald" LANGUAGES C)

if(WIN32 AND NOT ${CMAKE_SIZEOF_VOID_P} EQUAL 4)
    message(FATAL_ERROR "On Windows, Pidgin 2 is 32 bit only.")
endif()

find_package(PkgConfig QUIET)
if (${PKG_CONFIG_FOUND})
    pkg_check_modules(PURPLE REQUIRED purple)
    pkg_get_variable(PURPLE_PLUGIN_DIR purple plugindir)
    pkg_get_variable(PURPLE_DATA_DIR purple datarootdir)
    pkg_check_modules(JSON REQUIRED json-glib-1.0)
    pkg_check_modules(PIXBUF gdk-pixbuf-2.0)
elseif(WIN32)
    message(STATUS "Trying win32 auto-configuration...")
    add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/win32)
elseif(${Purple_FOUND})
    message(STATUS "Purple was configured manually. Proceeding without checks.")
else()
    message(FATAL "pkg-config not found. Please configure manually and set Purple_FOUND to YES.")
endif()
find_package(Threads REQUIRED)

message(STATUS "INCLUDE_DIRS: ${PURPLE_INCLUDE_DIRS} ${JSON_INCLUDE_DIRS} ${PIXBUF_INCLUDE_DIRS}")
message(STATUS "LIBRARY_DIRS: ${PURPLE_LIBRARY_DIRS}")
message(STATUS "LIBRARIES: ${PURPLE_LIBRARIES} ${JSON_LIBRARIES} ${PIXBUF_LIBRARIES}")

set(TARGET_NAME signald)
add_library(${TARGET_NAME} SHARED 
    src/attachments.c
    src/comms.c
    src/contacts.c
    src/groups.c
    src/libsignald.c
    src/link.c
    src/login.c
    src/message.c
    src/signald_procmgmt.c
    src/attachments.h
    src/comms.h
    src/contacts.h
    src/groups.h
    src/link.h
    src/login.h
    src/message.h
    src/purple_compat.h
    src/signald_procmgmt.h
    src/input.h
    src/input.c
    src/options.h
    src/options.c
    src/receipt.h
    src/receipt.c
    src/defines.h
    src/structs.h
    src/admin.h
    src/admin.c
    src/status.c
    src/status.h
    src/interface.c
    src/interface.h
    src/reply.h
    src/reply.c
    src/json-utils.h
    submodules/MegaMimes/src/MegaMimes.c
    submodules/QR-Code-generator/c/qrcodegen.c
)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" PLUGIN_VERSION)
target_compile_definitions(${TARGET_NAME} PRIVATE PLUGIN_VERSION=${PLUGIN_VERSION})
target_include_directories(${TARGET_NAME} PRIVATE ${PURPLE_INCLUDE_DIRS} ${JSON_INCLUDE_DIRS} ${PIXBUF_INCLUDE_DIRS} submodules/MegaMimes/src/ submodules/QR-Code-generator/c/) # https://sourceforge.net/p/mingw-w64/discussion/723797/thread/4c8ecdbe0d/
target_link_libraries(${TARGET_NAME} PRIVATE ${PURPLE_LIBRARIES} ${JSON_LIBRARIES} ${PIXBUF_LIBRARIES} Threads::Threads)
set_target_properties(${TARGET_NAME} PROPERTIES PREFIX "lib")

message(STATUS "PURPLE_PLUGIN_DIR: ${PURPLE_PLUGIN_DIR}")
if (WIN32)
    install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION "${PURPLE_PLUGIN_DIR}")
else()
    install(TARGETS ${TARGET_NAME} DESTINATION "${PURPLE_PLUGIN_DIR}")
endif()

install(DIRECTORY "pixmaps" DESTINATION "${PURPLE_DATA_DIR}" FILES_MATCHING PATTERN "*.png")
