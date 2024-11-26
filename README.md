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

### VSCode

需要安装的插件：

- C/C++
- Code Runner 

## 编译

> <font color="red">注意:
> 
> qemu 7.1开始不再支持-soundhw，</font> 
> 
> qemu 7.1之前的版本要把Makefile中的
> ```
> -audio pa,model=sb16
> ```
> 改成
> ```
> -soundhw sb16
> ```

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
