use std::cell::RefCell;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::Write;
use std::path::PathBuf;
use std::sync::Arc;
use toml::Value;

use crate::config::DebugLevel;
use crate::dependency;

#[derive(Debug, Clone, Copy)]
pub enum RustcTargetType {
    Builtin,
    Custom,
}

#[derive(Debug, Clone)]
pub struct RustConfig {
    pub target: String,
    pub target_type: RustcTargetType,
    pub cargo_toml: RefCell<Option<PathBuf>>,
    rust_flags: Vec<String>,
    modules: Arc<RefCell<RustModules>>,
    force_update: Arc<RefCell<bool>>,
}

#[derive(Debug, Clone, Copy)]
enum ModuleType {
    Exist, // 存在 mod.rs 的目录
    NotExist,
}

#[derive(Debug)]
struct ModuleNode {
    module_type: ModuleType,
    children: HashMap<String, ModuleNode>,
}

impl ModuleNode {
    fn new(module_type: ModuleType) -> Self {
        ModuleNode {
            module_type,
            children: HashMap::new(),
        }
    }

    fn add_child(&mut self, name: &str, module_type: ModuleType) -> &mut ModuleNode {
        let entry = self.children.entry(name.to_string());
        entry.or_insert_with(|| ModuleNode::new(module_type))
    }
}

#[derive(Debug)]
pub struct RustModules {
    root: ModuleNode,
}

impl RustModules {
    pub fn new() -> Self {
        RustModules {
            root: ModuleNode::new(ModuleType::Exist),
        }
    }

    pub fn add_path(&mut self, cargo_toml: &PathBuf, path: &PathBuf) {
        if !cargo_toml.exists() || !cargo_toml.is_file() {
            panic!("Cargo.template.toml不存在!");
        }
        let path = path
            .strip_prefix(cargo_toml.parent().unwrap())
            .expect("路径不在Cargo.template.toml目录下");
        let components = path
            .components()
            .map(|v| v.as_os_str().to_string_lossy().to_string());

        let mut entry = &mut self.root;
        let mut path = cargo_toml.parent().unwrap().to_path_buf();
        for component in components {
            let tmp_path = path.join(&component);
            let mod_rs = tmp_path.join("mod.rs");
            let module_type = if mod_rs.exists() || tmp_path.with_extension("rs").exists() {
                ModuleType::Exist
            } else {
                ModuleType::NotExist
            };
            entry = entry.add_child(&component, module_type);
            path = tmp_path;
            if let ModuleType::Exist = module_type {
                break;
            }
        }
    }
}

impl RustConfig {
    pub fn new(target: String, rust_flags: Vec<String>, work_dir: &PathBuf) -> Self {
        if target.ends_with(".json") {
            let work_dir = work_dir.canonicalize().unwrap();
            let target = work_dir.join(target).to_string_lossy().to_string();
            RustConfig {
                target,
                target_type: RustcTargetType::Custom,
                rust_flags,
                modules: Arc::new(RefCell::new(RustModules::new())),
                force_update: Arc::new(RefCell::new(false)),
                cargo_toml: RefCell::new(None),
            }
        } else {
            RustConfig {
                target,
                target_type: RustcTargetType::Builtin,
                rust_flags,
                modules: Arc::new(RefCell::new(RustModules::new())),
                force_update: Arc::new(RefCell::new(false)),
                cargo_toml: RefCell::new(None),
            }
        }
    }

    pub fn use_cargo_toml(&mut self, cargo_toml: PathBuf, work_dir: &PathBuf, out_dir: &PathBuf) {
        self.write_cargo_configs(&cargo_toml, work_dir, out_dir);
        if self.cargo_toml.borrow().is_some() {
            self.cargo_toml = RefCell::new(Some(cargo_toml));
        } else {
            self.cargo_toml.borrow_mut().replace(cargo_toml);
        }
    }

    fn write_module_declaration(
        &self,
        file: &mut File,
        node: &ModuleNode,
        indent_level: usize,
    ) -> std::io::Result<()> {
        let indent = "    ".repeat(indent_level);

        for (name, node) in node.children.iter().clone() {
            match node.module_type {
                ModuleType::NotExist => {
                    writeln!(file, "{}pub mod {} {{", indent, name)?;
                    self.write_module_declaration(file, node, indent_level + 1)?;
                    writeln!(file, "{}}}", indent)?;
                }
                ModuleType::Exist => {
                    writeln!(file, "{}pub mod {};", indent, name)?;
                }
            }
        }
        Ok(())
    }

    fn write_root_file(&self, work_dir: &PathBuf) -> std::io::Result<()> {
        let work_dir = work_dir.canonicalize()?;
        let template_path = work_dir.join("root.template.rs");
        let root_file_path = work_dir.join("root.rs");

        // 读取模板文件
        let template_content = fs::read_to_string(&template_path)?;

        // 创建输出文件
        let mut file = std::fs::File::create(&root_file_path)?;

        // 写入自动生成的开头注释
        writeln!(file, "// Auto-generated root file for this crate")?;
        writeln!(file, "// Target: {}", self.target)?;
        writeln!(file, "// Based on: {:?}\n", template_path)?;

        // 写入模板内容
        write!(file, "{}", template_content)?;

        // 确保最后有换行符
        if !template_content.ends_with('\n') {
            writeln!(file)?;
        }

        // 写入结尾的模块声明
        writeln!(file, "\n// Auto-generated module declarations")?;
        let modules = self.modules.borrow();
        self.write_module_declaration(&mut file, &modules.root, 0)?;

        Ok(())
    }

    fn write_cargo_config(&self, out_dir: &PathBuf) -> std::io::Result<()> {
        let cargo_config_path = out_dir.join(".cargo");
        if !cargo_config_path.exists() {
            fs::create_dir_all(&cargo_config_path)?;
        }
        let config_toml_path = cargo_config_path.canonicalize()?.join("config.toml");
        let mut file = fs::File::create(&config_toml_path)?;

        writeln!(file, "# Auto-generated Cargo config file")?;
        writeln!(file, "[build]")?;
        writeln!(file, "target = \"{}\"", self.target)?;
        writeln!(
            file,
            "target-dir = \"{}\"",
            out_dir.join("rustc_target/").display()
        )?;
        write!(file, "rustflags = [")?;
        for flag in self.rust_flags.iter() {
            // Let toml::Value handle escaping for quotes/backslashes in each flag.
            write!(file, "{},", Value::String(flag.clone()))?;
        }
        writeln!(file, "]")?;

        if let RustcTargetType::Custom = self.target_type {
            writeln!(file, "\n[unstable]")?;
            writeln!(file, "build-std = [\"core\", \"compiler_builtins\"]")?;
        }

        Ok(())
    }

    fn write_cargo_toml(
        &self,
        cargo_toml: &PathBuf,
        work_dir: &PathBuf,
        out_dir: &PathBuf,
    ) -> std::io::Result<()> {
        let cargo_toml = fs::read_to_string(cargo_toml)?;

        let mut lines: Vec<String> = cargo_toml.lines().map(|s| s.to_string()).collect();

        let mut lib_index: Option<usize> = None;
        for (i, line) in lines.iter().enumerate() {
            if line.trim_start().starts_with("[lib]") {
                lib_index = Some(i);
                break;
            }
        }

        if let Some(idx) = lib_index {
            // 找到下一个以 '[' 开头的 section 行位置（如果没有则使用文件末尾）
            let mut next_idx = lines.len();
            for (j, line) in lines.iter().enumerate().skip(idx + 1) {
                if line.trim_start().starts_with('[') {
                    next_idx = j;
                    break;
                }
            }

            // 在 [lib] 和下一个 section 之间检查是否已有 path 字段
            for line in &lines[idx + 1..next_idx] {
                if let Some(eq_pos) = line.find('=') {
                    let key = line[..eq_pos].trim();
                    if key == "path" {
                        return Err(std::io::Error::new(
                            std::io::ErrorKind::Other,
                            "Cargo.template.toml 中 [lib] 已包含 path 字段",
                        ));
                    }
                } else if line.trim_start().starts_with("path") {
                    // 保守检查，处理没有等号的异常格式
                    return Err(std::io::Error::new(
                        std::io::ErrorKind::Other,
                        "Cargo.template.toml 中 [lib] 已包含 path 字段",
                    ));
                }
            }

            // 在该 section 的末尾插入 path 字段（即在 next_idx 处插入）
            lines.insert(
                next_idx,
                format!("path = \"{}\"", work_dir.join("root.rs").display()),
            );
        } else {
            // 如果没有 [lib] 节，则追加一个新的 [lib] 节并写入 path
            if !cargo_toml.ends_with('\n') {
                lines.push(String::new());
            }
            lines.push("[lib]".to_string());
            lines.push("path = \"./root.rs\"".to_string());
        }

        let new_contents = lines.join("\n");
        let cargo_toml_path = out_dir.join("Cargo.toml");
        fs::write(cargo_toml_path, new_contents)?;

        Ok(())
    }

    pub fn add_file(&self, file_path: &PathBuf) {
        let mut modules = self.modules.borrow_mut();
        match self.cargo_toml.borrow().as_ref() {
            Some(cargo_toml) => {
                modules.add_path(cargo_toml, file_path);
            }
            None => {
                panic!("使用了rust文件但没有定义Cargo.template.toml!");
            }
        }
    }

    pub fn set_force_update(&self, value: bool) {
        let mut force_update = self.force_update.borrow_mut();
        *force_update = value;
    }

    fn need_update(&self) -> bool {
        *self.force_update.borrow()
    }

    fn is_empty(&self) -> bool {
        let modules = self.modules.borrow();
        modules.root.children.is_empty()
    }

    fn write_cargo_configs(&self, cargo_toml: &PathBuf, work_dir: &PathBuf, out_dir: &PathBuf) {
        if self.need_update() {
            self.write_cargo_toml(cargo_toml, work_dir, out_dir)
                .expect("写入Cargo.template.toml失败");
            self.write_cargo_config(out_dir)
                .expect("写入cargo config失败");
        }
    }

    pub fn write_configs(
        &self,
        work_dir: &PathBuf,
        out_dir: &PathBuf,
        debug_level: DebugLevel,
    ) -> Option<PathBuf> {
        let cargo_toml = work_dir.join("Cargo.template.toml");
        if cargo_toml.exists() && cargo_toml.is_file() {
            let root_file = work_dir.join("root.rs");
            if self.need_update() || !(root_file.exists() && root_file.is_file()) {
                self.write_root_file(work_dir).expect("写入root.rs失败");

                let target_name = self
                    .target
                    .split(".")
                    .next()
                    .unwrap()
                    .rsplit('/')
                    .next()
                    .unwrap();
                dependency::do_rust_dependency(
                    &self,
                    &work_dir.join("root.rs"),
                    &out_dir,
                    &target_name,
                    debug_level == DebugLevel::Release,
                );
                return Some(out_dir.join("rlib.a"));
            }
        }
        None
    }
}
