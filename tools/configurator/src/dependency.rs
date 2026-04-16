use std::path::PathBuf;

use std::process::Command;

use crate::config::{CompilerConfig, Config};
use crate::rustc_target::{RustConfig, RustcTargetType};
use std::fs::OpenOptions;
use std::io::Write;

pub fn do_cc_dependency<'a>(compiler: &'a CompilerConfig, file: &PathBuf, out_dir: &PathBuf) {
    let file_name = file.file_name().unwrap().to_str().unwrap();
    let out_cmd_file = out_dir.join(format!("{}.o.cmd", file_name));
    let out_file = out_dir.join(format!("{}.o", file_name));
    let mut command = Command::new(compiler.executable.as_os_str());
    command.args(&compiler.flags);
    command.args(["-M", "-MF"]);
    command.arg(&out_cmd_file);
    command.arg("-MT");
    command.arg(&out_file);
    command.arg(file);

    let status = command.status().expect("无法执行编译器以生成依赖文件");
    if !status.success() {
        eprintln!("错误: 编译器生成依赖文件失败，状态码: {}", status);
        std::process::exit(1);
    }

    let mut cmd_file = OpenOptions::new()
        .append(true)
        .open(&out_cmd_file)
        .expect(&format!("无法打开{}", out_cmd_file.display()));

    writeln!(cmd_file, "\t@echo CC $@").unwrap();
    writeln!(
        cmd_file,
        "\t@{} {} -c $< -o $@",
        compiler.executable.display(),
        compiler.flags.join(" ")
    )
    .unwrap();
}

pub fn do_asm_dependency<'a>(assembler: &'a CompilerConfig, file: &PathBuf, out_dir: &PathBuf) {
    let file_name = file.file_name().unwrap().to_str().unwrap();
    let out_cmd_file = out_dir.join(format!("{}.o.cmd", file_name));
    let out_file = out_dir.join(format!("{}.o", file_name));

    let mut cmd_file = OpenOptions::new()
        .create(true)
        .truncate(true)
        .write(true)
        .open(&out_cmd_file)
        .expect(&format!("无法打开{}", out_cmd_file.display()));

    // 禁用所有默认的后缀规则，防止 make 使用内置规则编译
    writeln!(cmd_file, ".SUFFIXES:\n").unwrap();

    writeln!(cmd_file, "{}: {}", out_file.display(), file.display()).unwrap();

    writeln!(cmd_file, "\t@echo AS $@").unwrap();
    writeln!(
        cmd_file,
        "\t@{} {} $< -o $@",
        assembler.executable.display(),
        assembler.flags.join(" ")
    )
    .unwrap();
}

pub fn do_rust_dependency<'a>(
    rustc: &'a RustConfig,
    file: &PathBuf,
    out_dir: &PathBuf,
    target_name: &str,
    is_release: bool,
) {
    let out_cmd_file = out_dir.join("rlib.a.cmd");
    let out_file = out_dir.join("rlib.a");

    let mut cmd_file = OpenOptions::new()
        .create(true)
        .truncate(true)
        .write(true)
        .open(&out_cmd_file)
        .expect(&format!("无法打开{}", out_cmd_file.display()));

    // 禁用所有默认的后缀规则，防止 make 使用内置规则编译
    writeln!(cmd_file, ".SUFFIXES:\n").unwrap();

    writeln!(cmd_file, "{}: FORCE", out_file.display()).unwrap();

    writeln!(cmd_file, "\t@echo RUSTC $@").unwrap();
    writeln!(
        cmd_file,
        "\t@rustup run {}",
        match rustc.target_type {
            RustcTargetType::Builtin => {
                "nightly cargo rustc"
            }
            RustcTargetType::Custom => {
                "nightly cargo rustc"
            }
        },
    )
    .unwrap();

    writeln!(cmd_file, "\t@echo COPY $@").unwrap();
    if is_release {
        writeln!(
            cmd_file,
            "\t@cp ./rustc_target/{}/release/lib*.a $@",
            target_name
        )
        .unwrap();
    } else {
        writeln!(
            cmd_file,
            "\t@cp ./rustc_target/{}/debug/lib*.a $@",
            target_name
        )
        .unwrap();
    }

    writeln!(cmd_file, "\nFORCE:").unwrap();
}

pub fn do_dir_dependency<'a>(
    config: &Config,
    out_dir: &PathBuf,
    files: Vec<PathBuf>,
    libs: Vec<PathBuf>,
    dirs: &Vec<PathBuf>,
) {
    let out_cmd_file = out_dir.join("built-in.o.cmd");

    let mut cmd_file = OpenOptions::new()
        .create(true)
        .truncate(true)
        .write(true)
        .open(&out_cmd_file)
        .expect("无法打开目录依赖文件以追加目录");

    writeln!(cmd_file, "# 由 {} 自动生成", env!("CARGO_PKG_NAME")).unwrap();

    // 禁用所有默认的后缀规则，防止 make 使用内置规则编译
    writeln!(cmd_file, ".SUFFIXES:\n").unwrap();

    write!(cmd_file, "{}: ", out_dir.join("built-in.o").display()).unwrap();
    for file in &files {
        write!(
            cmd_file,
            "\\\n {}.o",
            out_dir.join(file.file_name().unwrap()).display()
        )
        .unwrap();
    }
    for lib in &libs {
        write!(
            cmd_file,
            "\\\n {}",
            out_dir.join(lib.file_name().unwrap()).display()
        )
        .unwrap();
    }
    for dir in dirs {
        write!(cmd_file, "\\\n {}", dir.join("built-in.o").display()).unwrap();
    }

    write!(cmd_file, "\n").unwrap();
    if files.is_empty() && dirs.is_empty() {
        // 没有输入文件时，不调用链接器以避免 "no input files" 错误，改为创建一个空占位文件
        writeln!(cmd_file, "\t@echo TOUCH $@").unwrap();
        writeln!(cmd_file, "\t@mkdir -p $(dir $@) 2>/dev/null || true").unwrap();
        writeln!(cmd_file, "\t@touch $@").unwrap();
    } else {
        writeln!(cmd_file, "\t@echo LD $@").unwrap();
        writeln!(
            cmd_file,
            "\t@{} {} --whole-archive -r $^ -o $@",
            config.tools.linker.executable.display(),
            config.tools.linker.flags.join(" ")
        )
        .unwrap();
    }
    for file in &files {
        writeln!(
            cmd_file,
            "-include {}.o.cmd",
            out_dir.join(file.file_name().unwrap()).display()
        )
        .unwrap();
    }
    for lib in &libs {
        writeln!(
            cmd_file,
            "-include {}.cmd",
            out_dir.join(lib.file_name().unwrap()).display()
        )
        .unwrap();
    }
    for dir in dirs {
        writeln!(
            cmd_file,
            "-include {}",
            dir.join("built-in.o.cmd").display()
        )
        .unwrap();
    }
}
