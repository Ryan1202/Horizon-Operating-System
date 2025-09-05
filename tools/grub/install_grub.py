import os
import subprocess
import shutil
import struct
import argparse
from grub_dep_detect import GrubModDependencyResolver

# 定义变量
imagetool_path = os.path.join('tools', 'bin', 'imagetool')
hd_img_path = 'hd0.img'
hd_size = '64M'
embed_area_size = '1M'
boot_img_path = 'boot.img'
core_image_path = 'core.img'
disk_dir_path = 'disk'
grub_cfg_path = 'grub.cfg'
prefix_path = "/boot/grub/"
cross_prefix = ""
default_grub_dir = "/usr/lib/grub/"
grub_mkimage_path = ""
default_mods =\
"normal biosdisk part_gpt part_msdos fat ext2 multiboot2 all_video"

def run_command(command, hide=False):
    if not hide:
        print("execute command: ", command)
    subprocess.run(command, shell=True, text=True, check=True)

def write_boot_sector(image_path, boot_img_path):
    print("copy boot.img to disk image")
    with open(image_path, 'r+b') as img, open(boot_img_path, 'rb') as boot_img:
        img.write(boot_img.read(440))

def write_core_img(image_path, core_img_path):
    print("copy core.img to disk image")
    with open(image_path, 'r+b') as img, open(core_img_path, 'rb') as core_img:
        img.seek(512)
        core_img_size = os.path.getsize(core_img_path)
        img.write(core_img.read(core_img_size))

def create_grub_directory(disk_image_path, disk_dir, grub_cfg_path):
    os.makedirs(f'{disk_dir}/boot/grub', exist_ok=True)
    run_command(f"{imagetool_path} {disk_image_path} mkdir /p0/boot/", hide=True)
    run_command(f"{imagetool_path} {disk_image_path} mkdir /p0/boot/grub/", hide=True)
    run_command(f"{imagetool_path} {disk_image_path} copy {grub_cfg_path} /p0/boot/grub/grub.cfg")

def install_grub(disk_image_path, grub_dir_path, platform, fs, mods):
    # 检查 hd0.img 是否存在
    flag = True
    if os.path.isfile(disk_image_path):
        print(f"{disk_image_path}已存在，要重新生成吗？(y/N):", end="")
        choice = input()
        if choice.lower() == 'y':
            flag = True
        else:
            flag = False
        if flag:
            os.remove(disk_image_path)

    if flag:
        # 创建空白磁盘
        run_command(f"{imagetool_path} {disk_image_path} new --size {hd_size}")

        # 创建分区表
        run_command(f"{imagetool_path} {disk_image_path} partition primary {fs} {embed_area_size} 100%")

        # 格式化分区
        run_command(f"{imagetool_path} {disk_image_path} format /p0/ {fs}")

    # 检查 boot.img 是否存在
    if not os.path.isfile(boot_img_path):
        # 复制 boot.img
        _grub_dir_path = os.path.join(grub_dir_path, platform)
        if not os.path.isfile(os.path.join(_grub_dir_path, 'boot.img')):
            print(f"{_grub_dir_path}boot.img不存在，请检查")
            return
        shutil.copy(os.path.join(grub_dir_path, 'boot.img'), boot_img_path)

    # 创建 core.img
    prefix_device = "(hd0,"
    if fs[0:3] == "fat":
        prefix_device += "msdos1"
    prefix_device += ')'
    resolver = GrubModDependencyResolver(os.path.join(grub_dir_path, platform))
    all_deps = " ".join(resolver.resolve_dependencies(mods.split()))
    run_command(f"{grub_mkimage_path} -O {platform} -o {core_image_path} --prefix \"{prefix_device + prefix_path}\" {all_deps}")

    # 写入引导扇区
    write_boot_sector(disk_image_path, boot_img_path)

    # 写入 core.img
    write_core_img(disk_image_path, core_image_path)

    # 创建 GRUB 目录并复制配置文件
    create_grub_directory(disk_image_path, disk_dir_path, grub_cfg_path)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="安装GRUB到磁盘镜像")
    parser.add_argument("--image", help="磁盘映像路径", default=hd_img_path)
    parser.add_argument("--platform", help="目标平台", required=True)
    parser.add_argument("--fs", help="文件系统", required=True)
    parser.add_argument("--mods", help="要额外附加的模块", default=default_mods)
    parser.add_argument("--grub-dir", help=f"GRUB平台目录(如{default_grub_dir})", default=default_grub_dir)
    parser.add_argument("--grub-mkimage", help="grub-mkimage路径")
    args = parser.parse_args()

    grub_mkimage_path = "grub-mkimage"
    if args.grub_dir == None:
        # 使用了默认设置，检查一下
        if os.name == "Darwin":
            if args.grub_mkimage == None:
                if args.platform == "i386-pc":
                    cross_prefix = "i686-elf-"
                elif args.platform == "x86_64-efi":
                    cross_prefix = "x86_64-elf-"
                grub_mkimage_path = cross_prefix + grub_mkimage_path

            def get_brew_prefix():
                try:
                    result = subprocess.run(['brew', '--prefix', cross_prefix + 'grub'], capture_output=True, text=True, check=True)
                    return result.stdout.strip()
                except subprocess.CalledProcessError:
                    print("Error: Unable to get Homebrew prefix. Make sure Homebrew is installed.")
                    exit(1)
            brew_prefix = get_brew_prefix()

            versions = sorted(os.listdir(brew_prefix))
            latest_version = versions[-1]
            grub_dir_path = os.path.join(brew_prefix, latest_version, 'lib', cross_prefix[:-1], 'grub')
            grub_mkimage_path = cross_prefix + grub_mkimage_path
        elif os.name == "nt":
            print("检测到为Windows,请输入grub路径：")
            path = input()
            grub_dir_path = path
            if args.grub_mkimage == None:
                grub_mkimage_path = cross_prefix + grub_mkimage_path
                grub_mkimage_path = os.path.join(grub_dir_path, grub_mkimage_path)
            else:
                grub_mkimage_path = args.grub_mkimage
        else:
            if args.grub_mkimage != None:
                grub_mkimage_path = args.grub_mkimage
    else:
        if args.grub_mkimage != None:
            grub_mkimage_path = args.grub_mkimage

    install_grub(args.image, grub_dir_path, args.platform, args.fs, args.mods)
