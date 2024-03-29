cmake_minimum_required(VERSION 3.10) # lowest version this was tested with

project("purple-signald" LANGUAGES C)

find_package(PkgConfig QUIET)
if (${PKG_CONFIG_FOUND})
    pkg_check_modules(PURPLE REQUIRED purple)
    pkg_get_variable(PURPLE_PLUGIN_DIR purple plugindir)
    pkg_get_variable(PURPLE_DATA_DIR purple datarootdir)
    pkg_check_modules(JSON REQUIRED json-glib-1.0)
    pkg_check_modules(PIXBUF gdk-pixbuf-2.0)
elseif(${Purple_FOUND})
    message(STATUS "Purple was configured manually. Proceeding without checks.")
else()
    message(FATAL "pkg-config not found. Please configure manually and set Purple_FOUND to YES.")
endif()
find_package(Threads REQUIRED)

add_subdirectory(src)

install(DIRECTORY "pixmaps" DESTINATION "${PURPLE_DATA_DIR}" FILES_MATCHING PATTERN "*.png")
