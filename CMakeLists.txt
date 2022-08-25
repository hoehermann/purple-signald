cmake_minimum_required(VERSION 3.24) # for COMPILE_WARNING_AS_ERROR

project("purple-signald" LANGUAGES C)

set(Purple "purple" CACHE STRING "Purple version to build against.")
set_property(CACHE Purple PROPERTY STRINGS "purple" "purple-3")

find_package(PkgConfig QUIET)
if (${PKG_CONFIG_FOUND})
    pkg_check_modules(PURPLE REQUIRED ${Purple})
    pkg_get_variable(PURPLE_PLUGIN_DIR ${Purple} plugindir)
    pkg_check_modules(JSON REQUIRED json-glib-1.0)
    pkg_check_modules(PIXBUF gdk-pixbuf-2.0)
elseif(${Purple_FOUND})
    message(STATUS "Purple was configured manually. Proceeding without checks.")
else()
    message(FATAL "pkg-config not found. Please configure manually and set Purple_FOUND to YES.")
endif()
find_package(Threads REQUIRED)

add_subdirectory(src)

if (${PURPLE_VERSION} VERSION_LESS "3.0.0")
    pkg_get_variable(PURPLE_DATA_DIR purple datarootdir)
    install(DIRECTORY "pixmaps" DESTINATION "${PURPLE_DATA_DIR}" FILES_MATCHING PATTERN "*.png")
endif()
