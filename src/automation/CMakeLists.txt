file(GLOB MODULE_UNITS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx"
)

file(GLOB IMPL_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

nr_add_library(nrautomation STATIC)
target_link_libraries(nrautomation
PUBLIC
    dependency
    nroptions
    utils
)
target_sources(nrautomation
    PRIVATE
        ${IMPL_SOURCES}
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULE_UNITS}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${MODULE_UNITS}
    ${IMPL_SOURCES}
)
