add_subdirectory(plugin)

if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()

plasma_install_package(package io.github.swim233.plasma-nethogs)
