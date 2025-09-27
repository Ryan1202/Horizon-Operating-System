cmake_minimum_required(VERSION 3.24)
project(HorizonOS C ASM)

set(CMAKE_C_STANDARD 11)
enable_language(ASM)

# 内核
add_subdirectory(src)

# 库
#add_subdirectory(libs)

# 工具 (Rust)
add_custom_target(tools
    COMMAND cargo build --manifest-path ${CMAKE_SOURCE_DIR}/tools/imagetool/Cargo.toml --release
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/imagetool
)

# 应用
#add_subdirectory(apps)

# qemu run
add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -E echo "Running QEMU..."
    # 后续可以写具体 qemu 命令
    COMMAND "qemu-system-x86_64" -no-reboot -monitor stdio -m 1024 
	-hda ${CMAKE_SOURCE_DIR}/hd0.img 
	-usb 
	-device usb-mouse 
	-device rtl8139,netdev=nc1 
	-netdev user,id=nc1,hostfwd=tcp::5555-:80 
	-object filter-dump,id=f1,netdev=nc1,file=dump.pcap 
	-readconfig ${CMAKE_SOURCE_DIR}/qemu-linux.cfg 
	-boot c
)
