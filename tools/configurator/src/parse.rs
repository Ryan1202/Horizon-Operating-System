use crate::{
    compile_commands::{CompileCommand, CompileCommands},
    config::Config,
    dependency,
};
use std::{
    borrow::Cow,
    fs::{self, File},
    io::{self, Read},
    path::PathBuf
};
use toml::Value;

struct DirectoryContext<'a> {
    config: Cow<'a, Config>,
    work_dir: PathBuf,
    out_dir: PathBuf,
    force_update: bool,
    toml: Option<Value>,
}

impl<'a> DirectoryContext<'a> {
    fn new(config: Cow<'a, Config>, work_dir: PathBuf, out_dir: PathBuf, force_update: bool) -> Self {
        DirectoryContext {
            config,
            work_dir,
            out_dir,
            force_update,
            toml: None,
        }
    }

    fn check_dir_update(&mut self) -> io::Result<()> {
        let config_file_path = self.work_dir.join("config.toml");
        let target_file_path = self.out_dir.join("built-in.o.cmd");

        let source_meta = match File::open(config_file_path) {
            Ok(mut file) => {
                self.toml = Some({
                    let mut buf = String::new();
                    file.read_to_string(&mut buf)?;
                    toml::from_str(&buf).map_err(|e| {
                        io::Error::new(
                            io::ErrorKind::InvalidData,
                            format!("无法解析 config.toml: {}", e),
                        )
                    })?
                });
                file.metadata()?
            }
            Err(e) => return Err(e),
        };
        let target_meta = match fs::metadata(&target_file_path) {
            Ok(metadata) => metadata,
            Err(_) => {
                self.force_update = true;
                return Ok(());
            }
        };
        if source_meta.modified()? > target_meta.modified()? {
            self.force_update = true;
        }
        Ok(())
    }

    fn enter_subdir(&'_ self, subdir: &str) -> DirectoryContext<'_> {
        let new_work_dir = self.work_dir.join(subdir);
        let new_out_dir = self.out_dir.join(subdir);

        if !new_out_dir.exists() || !new_out_dir.is_dir() {
            if let Err(e) = fs::create_dir_all(&new_out_dir) {
                eprintln!(
                    "错误: 无法创建子目录输出目录 {}: {}",
                    new_out_dir.display(),
                    e
                );
                std::process::exit(9);
            }
        }
        DirectoryContext {
            config: Cow::Borrowed(&self.config),
            work_dir: new_work_dir,
            out_dir: new_out_dir,
            force_update: self.force_update,
            toml: None,
        }
    }
}

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

pub fn parse_config<'a, 'b>(
    config: Cow<'a, Config>,
    work_dir: PathBuf,
    out_dir: PathBuf,
    directory: &'b str,
    force_update: bool,
) -> CompileCommands<'b> {
    let mut compile_commands = CompileCommands::new();
    let mut ctx = DirectoryContext::new(config, work_dir, out_dir, force_update);
    let mut real_dirs = Vec::new();

    ctx.check_dir_update().expect("检查目录更新失败！");
    if ctx.force_update {
        real_dirs.push(ctx.out_dir.clone());
    }

    let dirs = {
        let config_toml = ctx.toml.as_ref().expect("未获取到config.toml！");
        let (dirs, lds, output) = {
            let config = ctx.config.to_mut();
            let (mut dirs, lds) =
                parse_arch_config(config, &ctx.work_dir, &config_toml);
            dirs.extend(parse_string_array(&config_toml, "dir").unwrap_or(Vec::new()));

            let output = config_toml.get("output").and_then(|v| v.as_str()).unwrap();
            parse_includes(&ctx.work_dir, config, &config_toml);
            parse_macros(config, &config_toml);

            (dirs, lds, output)
        };

        let file_path = ctx.out_dir.join("Makefile");
        if !file_path.exists() {
            let mut makefile = File::create(&file_path).expect("无法创建 Makefile");
            use std::io::Write;
            writeln!(makefile, ".SUFFIXES:\n").unwrap();
            writeln!(makefile, ".PHONY: {}\n", output).unwrap();
            writeln!(
                makefile,
                "{}: {}",
                output,
                ctx.out_dir.join("built-in.o").display()
            )
            .unwrap();
            writeln!(makefile, "\t@echo LD $@").unwrap();
            writeln!(
                makefile,
                "\t@{} {} -T {} -o $@ $^",
                ctx.config.tools.linker.executable.display(),
                ctx.config.tools.linker.flags.join(" "),
                ctx.work_dir.join(lds).display()
            )
            .unwrap();

            writeln!(makefile, "-include built-in.o.cmd").unwrap();
            writeln!(makefile, "\nclean:\n\trm -r ./**/*.o").unwrap();
        }
        dirs
    };

    for dir in &dirs {
        let mut new_ctx = ctx.enter_subdir(dir);

        new_ctx.check_dir_update().expect("检查目录更新失败！");

        if new_ctx.force_update {
            real_dirs.push(new_ctx.out_dir.clone());
        }

        compile_commands.extend(parse_dir_config(new_ctx, directory));
    }

    let files = Vec::new();
    dependency::do_dir_dependency(&ctx.config, &ctx.out_dir, files, &real_dirs);

    compile_commands
}

// 因为ctx所有权传递给了这个函数，在函数结束时会被释放，所以CompileCommands需要和ctx使用不同的生命周期标记
fn parse_dir_config<'a, 'b>(ctx: DirectoryContext<'a>, directory: &'b str) -> CompileCommands<'b> {
    let mut compile_commands = CompileCommands::new();

    let config_toml = ctx.toml.as_ref().expect("未获取到config.toml！");
    let dirs = parse_string_array(config_toml, "dir").unwrap_or(Vec::new());
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
            let asm_file_path = ctx.work_dir.join(asm_file);
            if !asm_file_path.exists() || !asm_file_path.is_file() {
                eprintln!(
                    "错误: ASM 源文件不存在或不是文件: {}",
                    asm_file_path.display()
                );
                std::process::exit(7);
            }
            compile_commands.add_command(CompileCommand::new_asm(
                &ctx.config.tools.assembler,
                directory,
                &ctx.out_dir,
                &asm_file_path,
            ));

            files.push(asm_file_path.clone());
            if ctx.force_update {
                dependency::do_asm_dependency(
                    &ctx.config.tools.assembler,
                    &asm_file_path,
                    &ctx.out_dir,
                );
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
            let c_file_path = ctx.work_dir.join(c_file);
            if !c_file_path.exists() || !c_file_path.is_file() {
                eprintln!("错误: C 源文件不存在或不是文件: {}", c_file_path.display());
                std::process::exit(7);
            }
            compile_commands.add_command(CompileCommand::new_c(
                &ctx.config.tools.cc,
                directory,
                &ctx.out_dir,
                &c_file_path,
            ));

            files.push(c_file_path.clone());
            if ctx.force_update {
                dependency::do_cc_dependency(&ctx.config.tools.cc, &c_file_path, &ctx.out_dir);
            }
        }
    }

    for dir in &dirs {
        let mut new_ctx = ctx.enter_subdir(dir);

        new_ctx.check_dir_update().expect("检查目录更新失败！");

        if ctx.force_update {
            real_dirs.push(new_ctx.out_dir.clone());
        }

        let sub_compile_commands = parse_dir_config(new_ctx, directory);
        compile_commands.extend(sub_compile_commands);
    }

    if !files.is_empty() || !real_dirs.is_empty() {
        dependency::do_dir_dependency(&ctx.config, &ctx.out_dir, files, &real_dirs);
    }

    compile_commands
}
