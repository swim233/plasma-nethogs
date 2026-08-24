find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBPF REQUIRED IMPORTED_TARGET libbpf>=1.0)

find_program(BPFTOOL_EXECUTABLE bpftool REQUIRED
             DOC "bpftool, from the 'bpf' package on Arch and 'linux-tools' elsewhere")
find_program(CLANG_EXECUTABLE clang REQUIRED)
find_program(LLVM_STRIP_EXECUTABLE NAMES llvm-strip llvm-objcopy)

set(BPF_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/bpf)
file(MAKE_DIRECTORY ${BPF_GEN_DIR})

# Only DWARF gets dropped; .BTF must survive, since that is what libbpf uses to
# resolve CO-RE relocations against the running kernel.
if(LLVM_STRIP_EXECUTABLE)
    set(BPF_STRIP_COMMAND COMMAND ${LLVM_STRIP_EXECUTABLE} -g ${BPF_GEN_DIR}/netmon.bpf.o)
else()
    set(BPF_STRIP_COMMAND COMMAND ${CMAKE_STRIP} --strip-debug
                                  --keep-section=.BTF --keep-section=.BTF.ext
                                  ${BPF_GEN_DIR}/netmon.bpf.o)
endif()

# CO-RE means the vmlinux.h generated here does not tie the resulting object to
# this kernel; relocations are resolved against the running kernel's BTF at
# load time.
add_custom_command(
    OUTPUT ${BPF_GEN_DIR}/vmlinux.h
    COMMAND sh -c
            "'${BPFTOOL_EXECUTABLE}' btf dump file /sys/kernel/btf/vmlinux format c > '${BPF_GEN_DIR}/vmlinux.h'"
    COMMENT "Generating vmlinux.h from the running kernel's BTF"
    VERBATIM)

set(BPF_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/bpf/netmon.bpf.c)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(BPF_TARGET_ARCH __TARGET_ARCH_x86)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(BPF_TARGET_ARCH __TARGET_ARCH_arm64)
else()
    message(FATAL_ERROR "Unsupported architecture for the BPF target: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

add_custom_command(
    OUTPUT ${BPF_GEN_DIR}/netmon.bpf.o
    COMMAND ${CLANG_EXECUTABLE}
            -g -O2 -Wall -Werror -target bpf -mcpu=v3
            # bpftool's generated vmlinux.h is full of anonymous forward
            # declarations that this warning fires on.
            -Wno-missing-declarations
            -D${BPF_TARGET_ARCH}
            -I${BPF_GEN_DIR}
            -I${CMAKE_CURRENT_SOURCE_DIR}/bpf
            -c ${BPF_SOURCE}
            -o ${BPF_GEN_DIR}/netmon.bpf.o
    ${BPF_STRIP_COMMAND}
    DEPENDS ${BPF_SOURCE}
            ${CMAKE_CURRENT_SOURCE_DIR}/bpf/netmon_types.h
            ${BPF_GEN_DIR}/vmlinux.h
    COMMENT "Compiling netmon.bpf.c for the BPF target"
    VERBATIM)

add_custom_command(
    OUTPUT ${BPF_GEN_DIR}/netmon.skel.h
    COMMAND sh -c
            "'${BPFTOOL_EXECUTABLE}' gen skeleton '${BPF_GEN_DIR}/netmon.bpf.o' > '${BPF_GEN_DIR}/netmon.skel.h'"
    DEPENDS ${BPF_GEN_DIR}/netmon.bpf.o
    COMMENT "Generating netmon.skel.h"
    VERBATIM)

add_custom_target(netmon_skeleton DEPENDS ${BPF_GEN_DIR}/netmon.skel.h)

add_executable(pnmd
    src/main.cpp
    src/BpfCollector.cpp
    src/ProcessResolver.cpp
    src/Aggregator.cpp
    src/JsonWriter.cpp)

add_dependencies(pnmd netmon_skeleton)

target_include_directories(pnmd PRIVATE
    ${BPF_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bpf)

target_link_libraries(pnmd PRIVATE Qt6::Core PkgConfig::LIBBPF)

install(TARGETS pnmd ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
