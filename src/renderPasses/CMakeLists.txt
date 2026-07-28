file(GLOB_RECURSE MODULE_UNITS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx"
)

file(GLOB_RECURSE IMPL_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

nr_add_library(nrrenderpasses STATIC)
target_link_libraries(nrrenderpasses
PUBLIC
    nrapp
    nroptions
    nrrenderer
    nrscene
    nrresource
    nrrhi
    utils
    dependency
    dependency_shader_share
)
target_sources(nrrenderpasses
    PRIVATE
        ${IMPL_SOURCES}
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULE_UNITS}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${MODULE_UNITS}
    ${IMPL_SOURCES}
)
