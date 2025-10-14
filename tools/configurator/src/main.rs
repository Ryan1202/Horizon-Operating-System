use std::{borrow::Cow, fs, path::PathBuf};
use clap::Parser;

mod parse;
mod config;
mod compile_commands;
mod dependency;
use config::Config;
use toml::{self, Value};

use crate::parse::parse_config;

/// 配置工具命令行参数
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// 工作目录（工作目录路径）
    #[arg(short = 'w', long = "work-dir", value_name = "PATH")]
    work_dir: PathBuf,

    /// 输出目录（生成文件将写入此目录）
    #[arg(short = 'o', long = "out-dir", value_name = "PATH")]
    out_dir: PathBuf,

    #[arg(short = 'f', long = "force-update")]
    force_update: bool,

    #[arg(long = "compile-commands")]
    compile_commands: bool,
}

fn main() {
    let args = Args::parse();

    if !args.out_dir.exists() || !args.out_dir.is_dir() {
        if let Err(e) = std::fs::create_dir_all(&args.out_dir) {
            eprintln!(
                "错误: 无法创建输出目录 {}: {}",
                args.out_dir.display(),
                e
            );
            std::process::exit(4);
        } else {
            println!("已创建输出目录: {}", args.out_dir.display());
        }
    }

    println!("工作目录: {}", args.work_dir.canonicalize().unwrap().display());
    println!("输出目录: {}", args.out_dir.canonicalize().unwrap().display());

    // 验证工作目录存在且为目录
    if !args.work_dir.exists() {
        eprintln!("错误: 工作目录不存在: {}", args.work_dir.display());
        std::process::exit(2);
    }
    if !args.work_dir.is_dir() {
        eprintln!("错误: 工作目录不是一个目录: {}", args.work_dir.display());
        std::process::exit(3);
    }

    // 如果输出目录不存在则尝试创建
    if !args.out_dir.exists() {
        if let Err(e) = std::fs::create_dir_all(&args.out_dir) {
            eprintln!(
                "错误: 无法创建输出目录 {}: {}",
                args.out_dir.display(),
                e
            );
            std::process::exit(4);
        } else {
            println!("已创建输出目录: {}", args.out_dir.display());
        }
    } else if !args.out_dir.is_dir() {
        eprintln!("错误: 输出目录路径存在但不是目录: {}", args.out_dir.display());
        std::process::exit(5);
    }

    let config_file = args.work_dir.join("config.toml");
    if !config_file.exists() || !config_file.is_file() {
        eprintln!("错误: 配置文件不存在或不是文件: {}", config_file.display());
        std::process::exit(6);
    }
    let config_file = fs::read_to_string(config_file.as_path()).expect("无法读取配置文件");
    let config: Value = toml::from_str(&config_file).expect("无法解析配置文件");
    let config = Config::from(args.work_dir.clone(), config).expect("无法加载配置");

    let out_dir = args.out_dir.canonicalize().unwrap();
    let work_dir = args.work_dir.canonicalize().unwrap();
    let work_dir = work_dir.join("src/");

    let directory = work_dir.to_string_lossy().to_string();

    let config = Cow::Owned(config);
    let compile_commands = parse_config(config, work_dir, out_dir, &directory, args.force_update);

    if args.compile_commands {
        let out_file = args.work_dir.join("compile_commands.json");
        compile_commands
            .to_json(&out_file)
            .expect(&format!("无法写入 {}", out_file.display()));
        println!("已生成 {}", out_file.display());
    }
}