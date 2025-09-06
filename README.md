<h1 align="center">Horizon操作系统</h1>
<p align="center">
	<a href="https://github.com/Ryan1202/Horizon-Operating-System">
		<img src="https://img.shields.io/github/stars/Ryan1202/Horizon-Operating-System.svg?logo=GitHub" />
	</a>
	<a href="https://github.com/Ryan1202/Horizon-Operating-System">
		<img src="https://img.shields.io/github/forks/Ryan1202/Horizon-Operating-System.svg?logo=GitHub" />
	</a>
	<a href="https://github.com/Ryan1202/Horizon-Operating-System">
		<img src="https://img.shields.io/github/license/Ryan1202/Horizon-Operating-System.svg" />
	</a>
	<br/>
	是个半成品。。。
</p>

真正的主分支：[master](https://github.com/Ryan1202/Horizon-Operating-System/tree/master)

## 环境

建议使用```VSCode```开发

需要的工具(括号中为我使用的版本):
- dd(9.4)
- gcc(13.2.0)/clang(18.1.3)
- ld(2.42)/lld(18.1.3)
- make(4.3)
- nasm(2.16.01)
- qemu-system-i386(8.2.2)

此外，还需要python(执行grub安装脚本)和rust(编译工具)环境
- python(3.12.3)
- rust：[Install Rust - Rust Programming Language(rust-lang.org)](https://www.rust-lang.org/tools/install)

### Grub

在虚拟机中运行使用grub引导，所以需要先安装grub

#### Windows

使用Windows需要先下载 [Grub for Windows (2.12)](https://ftp.gnu.org/gnu/grub/grub-2.12-for-windows.zip) 并解压，在执行安装脚本时会提示输入grub路径

#### macOS

使用macOS需要使用homebrew安装i686-elf-grub

```
brew install i686-elf-grub
```

#### Linux

大多数Linux发行版默认使用grub引导，`install_grub.py`脚本可以自动从`/usr/lib/grub`找到模块目录

如果不是或者使用的并非x86 pc，需要自行安装grub

### VSCode

需要安装的插件：

- C/C++
- Code Runner 

## 编译

> <font color="red">注意:
> 
> qemu 7.1开始不再支持-soundhw，</font> 
> 
> qemu 7.1之前的版本要把
> ```
> -audio pa,model=sb16
> ```
> 改成
> ```
> -soundhw sb16
> ```
> 
> 使用WSL环境运行QEMU时可能需要手动配置`pulseaudio`，或者可以把qemu路径改为windows版qemu，如果不需要音频输出也可以直接删除这一行

### 虚拟硬盘

> 创建包含grub的虚拟硬盘
> ```
> make hd
> ```
>
> 写入disk目录下的文件
> ```
> make writehd
> ```

### 内核
```
make
```
###  库
```
make lib
```
### 应用程序
```
make app
```

## 运行

```
make run
```

## 调试运行

```
make run_dbg
```
