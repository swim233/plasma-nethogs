cmake_minimum_required(VERSION 3.20)

project(plasma-nethogs VERSION 1.0 LANGUAGES C CXX)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMDeprecationSettings)
include(ECMQmlModule)
include(ECMAddTests)
include(FeatureSummary)

# Qml is for the widget's snapshot reader, which is a compiled QML type because
# QML cannot read a local file without either a session-wide environment
# variable or a forked process; see plasmoid/plugin/StateWatcher.h.
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Qml)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Package)
find_package(Plasma REQUIRED)

ecm_set_disabled_deprecation_versions(QT 6.6 KF 6.0)

add_subdirectory(daemon)
add_subdirectory(plasmoid)

# The unit needs the resolved absolute path of the installed binary.
configure_file(systemd/plasma-nethogsd.service.in ${CMAKE_CURRENT_BINARY_DIR}/plasma-nethogsd.service @ONLY)
# ECM's SYSTEMDUNITDIR is lib/systemd, the parent of both unit trees.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plasma-nethogsd.service
        DESTINATION ${KDE_INSTALL_SYSTEMDUNITDIR}/system)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
