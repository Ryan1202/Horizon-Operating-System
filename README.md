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
- dd(8.30)
- gcc(8.3.0)/clang(13.0.1)
- kpartx
- ld(2.31.1)/lld(13.0.1)
- make(4.2.1)
- mkfs(2.38.1)
- nasm(2.14)
- qemu-system-i386(7.1.0)

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

> 创建
> ```
> sudo make hd
> ```
>
> 写入
> ```
> sudo make writehd
> ```

### 内核
```
make
```
###  库
```
make lib
```
### 用户程序
```
make app
```

## 运行

```
make run
```

## 调试

```
make run_dbg
```
