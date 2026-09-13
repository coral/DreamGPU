// SPDX-License-Identifier: GPL-2.0-or-later
use anyhow::{Context, Result};
use std::path::PathBuf;
fn main() -> Result<()> {
    let mut args = std::env::args().skip(1);
    let action = args
        .next()
        .context("usage: dreamgpu-build guest --root PATH --output PATH")?;
    let mut root = None;
    let mut output = None;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--root" => root = Some(PathBuf::from(args.next().context("missing root")?)),
            "--output" => output = Some(PathBuf::from(args.next().context("missing output")?)),
            _ => anyhow::bail!("unknown argument {arg}"),
        }
    }
    let root = root.context("--root required")?.canonicalize()?;
    let output = output.context("--output required")?;
    std::fs::create_dir_all(&output)?;
    let output = output.canonicalize()?;
    match action.as_str() {
        "guest" => dreamgpu_build::guest::build(&root, &output),
        "native" => dreamgpu_build::native::build(&root, &output),
        _ => anyhow::bail!("unknown component {action}"),
    }
}
