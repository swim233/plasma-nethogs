# A compiled QML module rather than QML shipped inside the applet package,
# because a Plasma package can only carry QML and assets -- there is no import
# path private to a package, so a compiled type has to be installed where the
# QML engine already looks. That is the cost of not forking a process per
# update; see the comment at the top of StateWatcher.h.
#
# The URI cannot be the applet id: a QML module URI is a dot-separated list of
# identifiers, and `plasma-nethogs` is not one.
add_library(plasmanethogsstate)

ecm_add_qml_module(plasmanethogsstate
    URI "io.github.swim233.nethogs"
    VERSION 1.0
    GENERATE_PLUGIN_SOURCE
    # Laid out under the build tree the way it will be laid out once installed,
    # so the development loop can point QML_IMPORT_PATH at build/bin and get the
    # module that was just built. See "Development loop" in CLAUDE.md.
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/io/github/swim233/nethogs
)

target_sources(plasmanethogsstate PRIVATE
    StateWatcher.cpp
    StateWatcher.h)

target_link_libraries(plasmanethogsstate PRIVATE Qt6::Core Qt6::Qml)

ecm_finalize_qml_module(plasmanethogsstate DESTINATION ${KDE_INSTALL_QMLDIR})

# The module is two libraries: the loadable plugin, which finalize installs under
# QMLDIR, and this one behind it, which the plugin has as a NEEDED entry and
# resolves through a runpath into LIBDIR. Installing only the plugin leaves an
# import that fails at runtime and builds and tests perfectly, because the build
# tree has both. Making this one STATIC does not avoid the second install
# either -- Qt then makes the plugin static too, and a static plugin cannot be
# loaded by plasmashell at all.
install(TARGETS plasmanethogsstate ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
