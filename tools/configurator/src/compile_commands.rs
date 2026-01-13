use std::{io, path::PathBuf};
use serde::{Serialize, Deserialize};
use crate::config::CompilerConfig;
use path_clean::PathClean;

#[derive(Debug, Serialize, Deserialize)]
pub struct CompileCommand<'a> {
    pub arguments: Vec<String>,
    directory: &'a str,
    file: String,
    output: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(bound(deserialize = "'de: 'a"))]
pub struct CompileCommands<'a> {
    pub commands: Vec<CompileCommand<'a>>,
}

impl<'a> CompileCommand<'a> {
    pub fn new_c(config: &CompilerConfig, directory: &'a str, output: &PathBuf, file: &PathBuf) -> Self {
        let mut arguments = Vec::new();
        arguments.push(config.executable.to_string_lossy().to_string());
        arguments.extend(config.flags.iter().map(|s| s.to_string()));

        // name: 输出目标文件名（保持相对名，例如 foo.o）
        let name = file.with_extension("c.o").file_name().unwrap()
            .to_string_lossy().to_string();

        // output: 将 output 目录与 name 组合，然后把结果转换为绝对路径。
        let output_path = output.join(name);
        let output = output_path.to_string_lossy().to_string();

        // file: 将 file 转为绝对路径字符串
        let file = file.canonicalize()
            .unwrap_or_else(|_| file.clean())
            .to_string_lossy().to_string();

        arguments.push("-c".to_string());
        arguments.push(file.clone());
        arguments.push("-o".to_string());
        arguments.push(output.clone());

        CompileCommand {
            arguments,
            directory,
            file,
            output,
        }
    }

    pub fn new_asm(config: &CompilerConfig, directory: &'a str, output: &PathBuf, file: &PathBuf) -> Self {
        let mut arguments = Vec::new();
        arguments.push(config.executable.to_string_lossy().to_string());
        arguments.extend(config.flags.iter().map(|s| s.to_string()));

        // name: 输出目标文件名（保持相对名，例如 foo.o）
        let name = file.with_extension("asm.o").file_name().unwrap()
            .to_string_lossy().to_string();

        // output: 将 output 目录与 name 组合，然后把结果转换为绝对路径。
        let output_path = output.join(name);
        let output = output_path.to_string_lossy().to_string();

        // file: 将 file 转为绝对路径字符串
        let file = file.canonicalize()
            .unwrap_or_else(|_| file.clean())
            .to_string_lossy().to_string();
        
        arguments.push(file.clone());
        arguments.push("-o".to_string());
        arguments.push(output.clone());

        CompileCommand {
            arguments,
            directory,
            file,
            output,
        }
    }
}

impl<'a> CompileCommands<'a> {
    pub fn new() -> Self {
        CompileCommands { commands: Vec::new() }
    }

    pub fn add_command(&mut self, command: CompileCommand<'a>) {
        self.commands.push(command);
    }

    pub fn extend(&mut self, other: CompileCommands<'a>) {
        self.commands.extend(other.commands);
    }

    pub fn to_json(&self, out_file: &PathBuf) -> io::Result<()> {
        let json = serde_json::to_string_pretty(&self.commands)?;
        std::fs::write(out_file, json)?;
        Ok(())
    }
}