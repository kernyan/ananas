project(kernel ASM C CXX)
cmake_minimum_required(VERSION 3.9)

set(COMMON_FLAGS "")
# ensure we don't drag in external things
set(COMMON_FLAGS "${COMMON_FLAGS} -fno-builtin -nostdlib -nostdinc")
set(COMMON_FLAGS "${COMMON_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/../include") # for global includes
set(COMMON_FLAGS "${COMMON_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/include") # for kernel/
set(COMMON_FLAGS "${COMMON_FLAGS} -I${CMAKE_CURRENT_BINARY_DIR}/include")
set(COMMON_FLAGS "${COMMON_FLAGS} -DKERNEL")

# machine-dependent flags
set(COMMON_FLAGS "${COMMON_FLAGS} -m64 -march=athlon64 -mcmodel=large")
set(COMMON_FLAGS "${COMMON_FLAGS} -mno-red-zone -mno-sse -fno-stack-protector")

set(COMMON_FLAGS "${COMMON_FLAGS} -O2 -g")

set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -DASM ${COMMON_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wno-attributes ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17  -fno-rtti -fno-exceptions ${COMMON_FLAGS}")

set(CMAKE_CXX_LINK_EXECUTABLE "ld <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS>  -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_FLAGS "-b elf64-x86-64 -T ${CMAKE_CURRENT_SOURCE_DIR}/conf/ld.amd64 -nostdlib -nodefaultlibs")

function(build_object dest src opts)
	string(REPLACE " " ";" flags ${CMAKE_CXX_FLAGS})
	add_custom_command(
		OUTPUT ${dest}
		COMMAND ${CMAKE_CXX_COMPILER} ${flags} ${opts} -c -o ${CMAKE_CURRENT_BINARY_DIR}/${dest} ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${opts}
	)
endfunction()

# inspired by https://stackoverflow.com/questions/31538466/keeping-file-hierarchy-across-subdirectories-in-cmake
function(add_sources)
	foreach(_s IN ITEMS ${ARGV})
		get_filename_component(_a "${_s}" ABSOLUTE)
		set_property(GLOBAL APPEND PROPERTY SourceList "${_a}")
	endforeach()
endfunction()

#
add_subdirectory(arch)
add_subdirectory(dev)
add_subdirectory(fs)
add_subdirectory(gdb)
add_subdirectory(kdb)
add_subdirectory(kern)
add_subdirectory(net)
add_subdirectory(lib)
add_subdirectory(sys)
add_subdirectory(vfs)
add_subdirectory(vm)

get_property(SOURCES GLOBAL PROPERTY SourceList)
set(SOURCES ${SOURCES} include/_gen/syscalls.h include/_gen/syscalls.inc.h)
add_executable(kernel_elf arch/amd64/prelude.S ${SOURCES} arch/amd64/postlude.S )
set_target_properties(kernel_elf PROPERTIES OUTPUT_NAME "kernel.elf")

add_custom_command(
    OUTPUT kernel.bin
    COMMENT "Converting kernel ELF to binary"
    DEPENDS kernel_elf
    COMMAND objcopy -O binary $<TARGET_FILE:kernel_elf> ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
)
add_custom_target(kernel_bin DEPENDS kernel.bin)

# add a custom target that combines the multiboot stub to get a bootable kernel
# XXX this will only work on unix-like systems
add_custom_command(
	OUTPUT kernel
    COMMENT "Merging kernel binary with multiboot loader"
	DEPENDS multiboot kernel.bin
	COMMAND cat $<TARGET_FILE:multiboot> ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin > ${CMAKE_CURRENT_BINARY_DIR}/kernel
)

add_custom_target(kernel_mb ALL DEPENDS kernel)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kernel DESTINATION .)

# prepare our include directories: we make a symlink to avoid unnecessary
# rebuilds
add_custom_target(kernel_md_param_h ALL
	COMMAND ${CMAKE_COMMAND} -E create_symlink
	${CMAKE_CURRENT_SOURCE_DIR}/include/kernel/amd64
	${CMAKE_CURRENT_BINARY_DIR}/include/kernel-md
)
add_dependencies(kernel_elf kernel_md_param_h)
add_custom_target(machine_types_h ALL
	COMMAND ${CMAKE_COMMAND} -E create_symlink
	${CMAKE_CURRENT_SOURCE_DIR}/../include/ananas/amd64
	${CMAKE_CURRENT_BINARY_DIR}/include/machine
)
add_dependencies(kernel_elf machine_types_h)

# generate syscalls.h from the kern's syscalls.in - we don't care about
# anything else so just write to /dev/null there
set(KERN_PATH ${CMAKE_SOURCE_DIR}/../kern)
add_custom_command(
	OUTPUT include/_gen/syscalls.h
	COMMAND ${KERN_PATH}/gen_syscalls.sh ${KERN_PATH}/syscalls.in /dev/null ${CMAKE_BINARY_DIR}/include/_gen/syscalls.h /dev/null
	DEPENDS ${KERN_PATH}/syscalls.in
)
#add_custom_target(syscall_gen DEPENDS include/_gen/syscalls.h)
#add_dependencies(kernel_elf syscall_gen)

# generate syscalls.inc.h like before. the .h extension is cheating, but this
# avoids CMake trying to build it
add_custom_command(
	OUTPUT include/_gen/syscalls.inc.h
	COMMAND ${KERN_PATH}/gen_syscalls.sh ${KERN_PATH}/syscalls.in /dev/null /dev/null ${CMAKE_BINARY_DIR}/include/_gen/syscalls.inc.h
	DEPENDS ${KERN_PATH}/syscalls.in
)
