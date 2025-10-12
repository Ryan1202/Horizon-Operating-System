use crate::{
    compile_commands::{CompileCommand, CompileCommands},
    config::Config,
    dependency,
};
use serde::de;
use std::{
    fs::{self, File},
    io::Read,
    path::PathBuf,
    process::Command,
};
use toml::Value;

fn parse_string_array(config_toml: &Value, key: &str) -> Option<Vec<String>> {
    config_toml.get(key).and_then(|v| {
        Some(
            v.as_array()?
                .iter()
                .filter_map(|v| v.as_str())
                .map(String::from)
                .collect::<Vec<String>>(),
        )
    })
}

fn parse_includes(work_dir: &PathBuf, config: &mut Config, config_toml: &Value) {
    let includes = parse_string_array(config_toml, "includes");
    if let Some(includes) = includes {
        config.tools.cc.flags.extend(
            includes
                .iter()
                .map(|s| format!("-I{}{}", work_dir.display(), s)),
        );
        config.tools.assembler.flags.extend(
            includes
                .iter()
                .map(|s| format!("-I{}{}", work_dir.display(), s)),
        );
    }
}

fn parse_macros(config: &mut Config, config_toml: &Value) {
    let macros = config_toml.get("macro").and_then(|v| {
        Some(
            v.as_table()?
                .iter()
                .filter_map(|(k, v)| {
                    let v = v.as_str()?;
                    Some((k, v))
                })
                .collect::<Vec<(&String, &str)>>(),
        )
    });
    if let Some(macros) = macros {
        config
            .tools
            .cc
            .flags
            .extend(macros.iter().map(|(k, v)| format!("-D{}={}", k, v)));
        config
            .tools
            .assembler
            .flags
            .extend(macros.iter().map(|(k, v)| format!("-D{}={}", k, v)));
    }
}

pub fn parse_arch_config(
    config: &mut Config,
    work_dir: &PathBuf,
    config_toml: &Value,
) -> (Vec<String>, String) {
    let archs = config_toml.get("arch").and_then(|v| v.as_table()).unwrap();

    let arch = archs.get(&config.arch).unwrap_or_else(|| {
        eprintln!("错误: 配置文件中未找到目标架构的配置: {}", config.arch);
        std::process::exit(1);
    });

    let dirs = parse_string_array(arch, "dir").unwrap_or_else(|| {
        eprintln!("错误: 配置文件中目标架构缺少 'dir' 字段或格式不正确");
        std::process::exit(1);
    });

    let lds = arch.get("lds").and_then(Value::as_str).unwrap().to_string();

    parse_includes(work_dir, config, arch);
    parse_macros(config, arch);

    (dirs, lds)
}

pub fn parse_config<'a>(
    config: &'a mut Config,
    work_dir: PathBuf,
    out_dir: PathBuf,
    directory: &'a str,
    force_update: bool,
) -> CompileCommands<'a> {
    let mut compile_commands = CompileCommands::new();
    let config_file_path = work_dir.join("config.toml");
    let content = fs::read_to_string(&config_file_path).expect("src: 无法读取 config.toml");
    let config_toml: Value = toml::from_str(&content).expect("src: 无法解析 config.toml");

    let (mut dirs, lds) = parse_arch_config(config, &work_dir, &config_toml);
    dirs.extend(parse_string_array(&config_toml, "dir").unwrap_or(Vec::new()));
    let mut real_dirs = Vec::new();

    let output = config_toml.get("output").and_then(|v| v.as_str()).unwrap();
    parse_includes(&work_dir, config, &config_toml);
    parse_macros(config, &config_toml);

    for dir in &dirs {
        let cur_work_dir = work_dir.join(&dir);
        let cur_out_dir = out_dir.join(&dir);

        if !cur_out_dir.exists() || !cur_out_dir.is_dir() {
            if let Err(e) = fs::create_dir_all(&cur_out_dir) {
                eprintln!(
                    "错误: 无法创建子目录输出目录 {}: {}",
                    cur_out_dir.display(),
                    e
                );
                std::process::exit(9);
            }
        }

        let mut force_update = force_update;
        let source = cur_work_dir.join("config.toml");
        let target = cur_out_dir.join("built-in.o.cmd");
        if !source.exists() {
            eprintln!(
                "错误: 目录 {} 中缺少 config.toml 文件",
                cur_work_dir.display()
            );
            std::process::exit(7);
        }
        if target.exists() {
            let source_meta = fs::metadata(&source).expect("无法获取源文件元数据");
            let target_meta = fs::metadata(&target).expect("无法获取目标文件元数据");
            if source_meta.modified().unwrap() > target_meta.modified().unwrap() {
                force_update = true;
            }
        } else {
            force_update = true;
        }

        compile_commands.extend(parse_dir_config(
            &cur_work_dir,
            &cur_out_dir,
            directory,
            config,
            force_update,
        ));

        if force_update {
            // 配置文件有更新，清除子目录的中间文件强制make重新生成
            let pattern = format!("{}/**/*.o", cur_out_dir.display());
            Command::new("rm")
                .arg("-f")
                .arg(pattern)
                .status()
                .expect("无法清除中间文件");

            real_dirs.push(cur_out_dir);
        }
    }

    let files = Vec::new();
    dependency::do_dir_dependency(config, &out_dir, files, &real_dirs);

    let file_path = out_dir.join("Makefile");
    if !file_path.exists() {
        let mut makefile = File::create(&file_path).expect("无法创建 Makefile");
        use std::io::Write;
        writeln!(makefile, ".SUFFIXES:\n").unwrap();
        writeln!(makefile, ".PHONY: {}\n", output).unwrap();
        writeln!(
            makefile,
            "{}: {}",
            output,
            out_dir.join("built-in.o").display()
        )
        .unwrap();
        writeln!(makefile, "\t@echo LD $@").unwrap();
        writeln!(
            makefile,
            "\t@{} {} -T {} -o $@ $^",
            config.tools.linker.executable.display(),
            config.tools.linker.flags.join(" "),
            work_dir.join(lds).display()
        )
        .unwrap();

        writeln!(makefile, "-include built-in.o.cmd").unwrap();
        writeln!(makefile, "\nclean:\n\trm -r ./**/*.o").unwrap();
    }

    compile_commands
}

pub fn parse_dir_config<'a>(
    work_dir: &PathBuf,
    out_dir: &PathBuf,
    directory: &'a str,
    config: &'a Config,
    force_update: bool,
) -> CompileCommands<'a> {
    let config_file_path = work_dir.join("config.toml");
    let mut config_file = File::open(&config_file_path).expect(&format!(
        "错误: 目录 {} 中缺少 config.toml 文件",
        work_dir.display()
    ));
    let mut content = String::new();
    config_file
        .read_to_string(&mut content)
        .expect(&format!("无法读取 {}", &config_file_path.display()));
    let config_toml: Value =
        toml::from_str(&content).expect(&format!("无法解析 {}", &config_file_path.display()));
    let mut compile_commands = CompileCommands::new();

    let dirs = parse_string_array(&config_toml, "dir").unwrap_or(Vec::new());
    let mut real_dirs = Vec::new();
    let mut files = Vec::new();

    let asm_files = config_toml.get("asm").and_then(|v| {
        Some(
            v.as_array()?
                .iter()
                .filter_map(|v| v.as_str())
                .collect::<Vec<&str>>(),
        )
    });
    if let Some(asm_files) = asm_files {
        for asm_file in asm_files {
            let asm_file_path = work_dir.join(asm_file);
            if !asm_file_path.exists() || !asm_file_path.is_file() {
                eprintln!(
                    "错误: ASM 源文件不存在或不是文件: {}",
                    asm_file_path.display()
                );
                std::process::exit(7);
            }
            compile_commands.add_command(CompileCommand::new_asm(
                &config.tools.assembler,
                directory,
                &out_dir,
                &asm_file_path,
            ));

            files.push(asm_file_path.clone());
            if force_update {
                dependency::do_asm_dependency(&config.tools.assembler, &asm_file_path, &out_dir);
            }
        }
    }

    let c_files = config_toml.get("c").and_then(|v| {
        Some(
            v.as_array()?
                .iter()
                .filter_map(|v| v.as_str())
                .collect::<Vec<&str>>(),
        )
    });
    if let Some(c_files) = c_files {
        for c_file in c_files {
            let c_file_path = work_dir.join(c_file);
            if !c_file_path.exists() || !c_file_path.is_file() {
                eprintln!("错误: C 源文件不存在或不是文件: {}", c_file_path.display());
                std::process::exit(7);
            }
            compile_commands.add_command(CompileCommand::new_c(
                &config.tools.cc,
                directory,
                &out_dir,
                &c_file_path,
            ));

            files.push(c_file_path.clone());
            if force_update {
                dependency::do_cc_dependency(&config.tools.cc, &c_file_path, &out_dir);
            }
        }
    }

    for dir in &dirs {
        let sub_dir = work_dir.join(&dir);
        if !sub_dir.exists() || !sub_dir.is_dir() {
            eprintln!("错误: 子目录不存在或不是目录: {}", sub_dir.display());
            std::process::exit(8);
        }
        let sub_out_dir = out_dir.join(&dir);
        if !sub_out_dir.exists() {
            if let Err(e) = fs::create_dir_all(&sub_out_dir) {
                eprintln!(
                    "错误: 无法创建子目录输出目录 {}: {}",
                    sub_out_dir.display(),
                    e
                );
                std::process::exit(9);
            }
        } else if !sub_out_dir.is_dir() {
            eprintln!(
                "错误: 子目录输出目录路径存在但不是目录: {}",
                sub_out_dir.display()
            );
            std::process::exit(10);
        }

        let mut force_update = force_update;
        let source = sub_dir.join("config.toml");
        let target = sub_out_dir.join("built-in.o.cmd");
        if !source.exists() {
            eprintln!("错误: 目录 {} 中缺少 config.toml 文件", sub_dir.display());
            std::process::exit(7);
        }
        if target.exists() {
            let source_meta = fs::metadata(&source).expect("无法获取源文件元数据");
            let target_meta = fs::metadata(&target).expect("无法获取目标文件元数据");
            if source_meta.modified().unwrap() > target_meta.modified().unwrap() {
                force_update = true;
            }
        } else {
            force_update = true;
        }

        let sub_compile_commands =
            parse_dir_config(&sub_dir, &sub_out_dir, directory, config, force_update);
        compile_commands.extend(sub_compile_commands);

        if force_update {
            // 配置文件有更新，清除子目录的中间文件强制make重新生成
            let pattern = format!("{}/**/*.o", sub_out_dir.display());
            Command::new("rm")
                .arg("-f")
                .arg(pattern)
                .status()
                .expect("无法清除中间文件");

            real_dirs.push(sub_out_dir);
        }
    }

    if !files.is_empty() || !real_dirs.is_empty() {
        dependency::do_dir_dependency(config, &out_dir, files, &real_dirs);
    }

    compile_commands
}
