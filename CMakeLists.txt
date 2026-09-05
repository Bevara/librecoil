cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBRECOIL_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_recoil.c
)

SET(LIBRECOIL_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/librecoil.a
)

SET(LIBRECOIL_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(librecoil
        "${LIBRECOIL_SRC}"
        "${LIBRECOIL_LIBS}"
        []
        ""
        "${LIBRECOIL_INC}"
        ""
        "1")
