find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

# The reader is compiled into the test rather than linked from the QML module,
# so the test does not need the module resolvable on an import path.
ecm_add_test(
    tst_statewatcher.cpp
    ../plugin/StateWatcher.cpp
    ../plugin/StateWatcher.h
    TEST_NAME statewatcher
    LINK_LIBRARIES Qt6::Core Qt6::Qml Qt6::Test)

# tst_eventpropagation.qml is a QML test and needs a QQuickView, Plasma's QML
# imports and a platform plugin, none of which ctest can be relied on to have;
# see "Checks" in CLAUDE.md for how to run it.
