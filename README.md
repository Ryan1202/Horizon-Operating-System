<h1 align="center">Horizon操作系统</h1>
<p align="center">
<a href="https://github.com/Ryan1202/Horizon-Operating-System"><img src="https://img.shields.io/github/stars/Ryan1202/Horizon-Operating-System.svg" /></a>
<a href="https://github.com/Ryan1202/Horizon-Operating-System"><img src="https://img.shields.io/github/forks/Ryan1202/Horizon-Operating-System.svg" /></a>
<a href="https://github.com/Ryan1202/Horizon-Operating-System"><img src="https://img.shields.io/github/license/Ryan1202/Horizon-Operating-System.svg" /></a>
<br/>
是个半成品。。。
</p>

## 环境

建议使用```VSCode```开发

需要的工具:
- dd
- gcc
- kpartx
- make
- mkfs
- nasm
- qemu

### VSCode

需要安装的插件：

- C/C++
- Code Runner 

## 编译

> 虚拟硬盘
>>
>> 创建
>> ```
>> sudo make hd
>> ```
>> 
>> 写入
>> ```
>> sudo make writehd
>> ```
> 
> 内核
> ```
> make
> ```
>  库
> ```
> make lib
> ```
> 用户程序
> ```
> make app
> ```

## 运行

```
make run
```

## 调试

```
make run_dbg
```
