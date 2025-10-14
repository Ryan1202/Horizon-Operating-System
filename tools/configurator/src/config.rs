use std::{
    env,
    error::Error,
    path::{Path, PathBuf},
};

use toml::Value;

#[derive(Debug, Clone)]
pub struct CompilerConfig {
    pub executable: PathBuf,
    pub flags: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct Tools {
    pub cc: CompilerConfig,
    pub assembler: CompilerConfig,
    pub linker: CompilerConfig,
}

#[derive(Debug, Clone)]
pub struct Config {
    pub arch: String,
    pub tools: Tools,
}

impl Tools {
    fn new(
        cc_executable: PathBuf,
        cc_flags: Vec<String>,
        assembler_executable: PathBuf,
        assembler_flags: Vec<String>,
        linker_executable: PathBuf,
        linker_flags: Vec<String>,
    ) -> Self {
        Tools {
            cc: CompilerConfig {
                executable: cc_executable,
                flags: cc_flags,
            },
            assembler: CompilerConfig {
                executable: assembler_executable,
                flags: assembler_flags,
            },
            linker: CompilerConfig {
                executable: linker_executable,
                flags: linker_flags,
            },
        }
    }
}

/// 在工作目录下或 PATH 中查找可执行文件，返回第一个存在的绝对或相对路径
fn find_executable(work_dir: &Path, name: &str) -> Option<PathBuf> {
    let name_path = Path::new(name);

    // 如果给定的是包含路径分隔符的相对或绝对路径，先按此解释
    if name_path.is_absolute()
        || name.contains(std::path::MAIN_SEPARATOR)
        || name.contains('/')
        || name.contains('\\')
    {
        let cand = if name_path.is_absolute() {
            name_path.to_path_buf()
        } else {
            work_dir.join(name_path)
        };
        if cand.exists() && cand.is_file() {
            return Some(cand);
        }
        return None;
    }

    // 优先检查 work_dir/name
    let cand = work_dir.join(name);
    if cand.exists() && cand.is_file() {
        return Some(cand);
    }

    // 在 PATH 中查找
    if let Some(paths) = env::var_os("PATH") {
        for p in env::split_paths(&paths) {
            let cand = p.join(name);
            if cand.exists() && cand.is_file() {
                return Some(cand);
            }
            // On Windows, consider PATHEXT - but keep it simple for now
        }
    }

    None
}

impl Config {
    pub fn from(work_dir: PathBuf, config: Value) -> Result<Self, Box<dyn Error>> {
        let arch = config
            .get("arch")
            .and_then(Value::as_str)
            .ok_or("Missing or invalid 'arch' field")?
            .to_string();

        let tools_table = config
            .get("tools")
            .and_then(Value::as_table)
            .ok_or("Missing or invalid 'tools' field")?;

        let tools;

        let mut cc = String::from("gcc");
        let mut as_ = String::from("nasm");
        let mut ld = String::from("ld");
        let mut cflags = Vec::new();
        let mut asflags = Vec::new();
        let mut ldflags = Vec::new();
        for (name, tool_config) in tools_table {
            match name.as_str() {
                "cc" => {
                    cc = tool_config
                        .as_str()
                        .ok_or("Invalid 'cc' field")?
                        .to_string()
                }
                "as" => {
                    as_ = tool_config
                        .as_str()
                        .ok_or("Invalid 'as' field")?
                        .to_string()
                }
                "ld" => {
                    ld = tool_config
                        .as_str()
                        .ok_or("Invalid 'ld' field")?
                        .to_string()
                }
                "cflags" => {
                    cflags = tool_config
                        .as_array()
                        .ok_or("Invalid 'cflags' field")?
                        .iter()
                        .filter_map(Value::as_str)
                        .map(String::from)
                        .collect();
                    continue;
                }
                "asflags" => {
                    asflags = tool_config
                        .as_array()
                        .ok_or("Invalid 'asflags' field")?
                        .iter()
                        .filter_map(Value::as_str)
                        .map(String::from)
                        .collect();
                }
                "ldflags" => {
                    // 解析链接器标志
                    ldflags = tool_config
                        .as_array()
                        .ok_or("Invalid 'ldflags' field")?
                        .iter()
                        .filter_map(Value::as_str)
                        .map(String::from)
                        .collect();
                }
                _ => {}
            };
        }
        // 将可执行文件名解析为实际路径，优先在工作目录查找，然后在 PATH 中查找
        let cc_path = find_executable(&work_dir, &cc).ok_or(format!("C 编译器未找到: {}", cc))?;
        let as_path = find_executable(&work_dir, &as_).ok_or(format!("汇编器未找到: {}", as_))?;
        let ld_path = find_executable(&work_dir, &ld).ok_or(format!("链接器未找到: {}", ld))?;

        tools = Tools::new(cc_path, cflags, as_path, asflags, ld_path, ldflags);

        let debug_level = config
            .get("debug")
            .and_then(|x| x.get("level").and_then(Value::as_str));
        if let Some(debug_level) = debug_level {
            match debug_level {
                _ => {}
            }
        }

        Ok(Config {
            arch: arch,
            tools,
        })
    }
}
