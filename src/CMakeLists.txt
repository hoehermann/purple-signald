set(TARGET_NAME signald)
add_library(${TARGET_NAME} SHARED 
    purple-3/compat.h
    attachments.c
    comms.c
    contacts.c
    groups.c
    link.c
    login.c
    message.c
    signald_procmgmt.c
    attachments.h
    comms.h
    contacts.h
    groups.h
    link.h
    login.h
    message.h
    signald_procmgmt.h
    input.h
    input.c
    options.h
    options.c
    receipt.h
    receipt.c
    defines.h
    structs.h
    admin.h
    admin.c
    status.c
    status.h
    interface.c
    interface.h
    reply.h
    reply.c
    json-utils.h
    ../submodules/MegaMimes/src/MegaMimes.c
    ../submodules/QR-Code-generator/c/qrcodegen.c
)
if (${PURPLE_VERSION} VERSION_LESS "3.0.0")
target_sources(${TARGET_NAME} PRIVATE
    "libsignald.c"
)
else()
target_sources(${TARGET_NAME} PRIVATE
    "purple-3/prpl.c"
    "purple-3/prpl.h"
)
endif()
set_property(TARGET ${TARGET_NAME} PROPERTY COMPILE_WARNING_AS_ERROR ON)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" PLUGIN_VERSION)
target_compile_definitions(${TARGET_NAME} PRIVATE SIGNALD_PLUGIN_VERSION="${PLUGIN_VERSION}" G_LOG_USE_STRUCTURED G_LOG_DOMAIN="Purple-Signald")

target_include_directories(${TARGET_NAME} PRIVATE ${PURPLE_INCLUDE_DIRS} ${JSON_INCLUDE_DIRS} ${PIXBUF_INCLUDE_DIRS} ../submodules/MegaMimes/src/ ../submodules/QR-Code-generator/c/)
target_link_libraries(${TARGET_NAME} PRIVATE ${PURPLE_LIBRARIES} ${JSON_LIBRARIES} ${PIXBUF_LIBRARIES} Threads::Threads)
set_target_properties(${TARGET_NAME} PROPERTIES PREFIX "lib")

install(TARGETS ${TARGET_NAME} DESTINATION "${PURPLE_PLUGIN_DIR}")
