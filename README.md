# Horizon操作系统

是个半成品。。。

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

内核
```
make
```
库
```
make libs
```
用户程序
```
make apps
```

## 运行

```
make run
```

## 调试

```
make run_dbg
```
