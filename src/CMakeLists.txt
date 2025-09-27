cmake_minimum_required(VERSION 3.24)
project(HorizonOSKernel C ASM)

# 开启 ASM 支持
enable_language(ASM)

# include 目录
include_directories(
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/arch/x86/include
)

# 汇编文件列表
file(GLOB_RECURSE ASM_SRCS
    arch/x86/boot/*.asm
    arch/x86/kernel/*.asm
)

# C 文件列表
file(GLOB_RECURSE C_SRCS
    arch/x86/**/*.c
    fs/**/*.c
    driver/**/*.c
    kernel/*.c
    lib/**/*.c
    objects/**/*.c
)
list(REMOVE_ITEM C_SRCS
    "${CMAKE_SOURCE_DIR}/src/arch/x86/drivers/network/rtl8139.c"
    "${CMAKE_SOURCE_DIR}/src/arch/x86/drivers/usb/**.c"
)


# 输出内核 ELF
add_executable(kernel.elf ${C_SRCS} ${ASM_SRCS})

if(APPLE)
    set(CMAKE_LINKER x86_64-elf-ld)
else()
    set(CMAKE_LINKER ld)
endif()
set(CMAKE_EXE_LINKER_FLAGS "-m elf_i386 -g -no-pie -T ${CMAKE_SOURCE_DIR}/src/arch/x86/kernel.lds")


set_source_files_properties(${ASM_SRCS} PROPERTIES COMPILE_FLAGS "-g -f elf")

# 链接器脚本
set_target_properties(kernel.elf PROPERTIES LINK_FLAGS "-T ${CMAKE_SOURCE_DIR}/src/arch/x86/kernel.lds")

# 编译选项
target_compile_options(kernel.elf PRIVATE
    -march=i486 -fno-builtin -Wall -Wunused -m32 -std=gnu11
    -fno-stack-protector -nostdinc -nostdlib -O0 -DARCH_X86=32
    -DKERNEL_NAME=\"horizon\" -DKERNEL_VERSION=\"0.0.1\"
)
