// SPDX-License-Identifier: GPL-2.0-or-later
//! Native build-host compiler recorder; never linked into a guest artifact.
use std::{
    env,
    ffi::OsString,
    fs,
    io::Write,
    path::Path,
    process::Command,
    time::{SystemTime, UNIX_EPOCH},
};

fn quote(value: &str) -> String {
    let mut out = String::from("\"");
    for ch in value.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            ch if ch < ' ' => out.push_str(&format!("\\u{:04x}", ch as u32)),
            ch => out.push(ch),
        }
    }
    out.push('"');
    out
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut arguments = env::args_os().skip(1);
    let directory = arguments.next().ok_or("missing record directory")?;
    let compiler = arguments.next().ok_or("missing real compiler")?;
    let args: Vec<OsString> = arguments.collect();
    let source = args.iter().find(|arg| {
        let path = Path::new(arg);
        matches!(
            path.extension().and_then(|x| x.to_str()),
            Some("c" | "cc" | "cpp" | "cxx")
        )
    });
    if let Some(source) = source {
        fs::create_dir_all(&directory)?;
        let cwd = env::current_dir()?;
        let all: Vec<_> = std::iter::once(&compiler)
            .chain(args.iter())
            .map(|x| quote(&x.to_string_lossy()))
            .collect();
        let json = format!(
            "{{\"directory\":{},\"file\":{},\"arguments\":[{}]}}\n",
            quote(&cwd.to_string_lossy()),
            quote(&source.to_string_lossy()),
            all.join(",")
        );
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
        let path = Path::new(&directory).join(format!("{}-{stamp}.json", std::process::id()));
        let mut file = fs::OpenOptions::new()
            .create_new(true)
            .write(true)
            .open(path)?;
        file.write_all(json.as_bytes())?;
    }
    let mut command = Command::new(compiler);
    command.args(args);
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        Err(command.exec().into())
    }
    #[cfg(not(unix))]
    {
        let status = command.status()?;
        std::process::exit(status.code().unwrap_or(1));
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn arguments_retain_quotes_backslashes_and_control_characters() {
        assert_eq!(
            super::quote("a\"b\\c\n\t\0"),
            "\"a\\\"b\\\\c\\n\\t\\u0000\""
        );
    }
}
