set(VEDIS_DIR ${CMAKE_SOURCE_DIR}/lib/vedis)
set(VEDIS_INCLUDE_DIR ${VEDIS_DIR})

add_library(vedis STATIC ${VEDIS_DIR}/vedis.c ${VEDIS_DIR}/vedis.h)

set(VEDIS_LIBRARY vedis)

file(STRINGS ${VEDIS_DIR}/vedis.h _VEDIS_VERSION_LINE REGEX "#define[ \t]+VEDIS_VERSION[ \t]+\"(.*)\".*")

if(${_VEDIS_VERSION_LINE} MATCHES "[^k]+VEDIS_VERSION[ \t]+\"(([0-9]\\.)+[0-9])\"")
    set(VEDIS_VERSION ${CMAKE_MATCH_1})
else()
    set(VEDIS_VERSION "Unknown")
endif()