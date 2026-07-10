// Offline replay: reconstructs the LogicalValue tree cargo-fuzz derived from
// each raw corpus file (identical generate() call as the fuzz targets) and
// prints it as one JSON line per file, for the oracle-validation step
// (scripts/generate-fuzz-corpus.py) to consume. Never touches the encoders —
// this tool only recovers the logical-value input, not any encoded bytes.
use std::env;
use std::fs;
use std::io::{self, Write};

use adapter::logical_value::to_json;
use arbitrary::Unstructured;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: replay <corpus-dir>");
        std::process::exit(2);
    }
    let dir = &args[1];
    let stdout = io::stdout();
    let mut out = stdout.lock();

    let mut entries: Vec<_> = fs::read_dir(dir)
        .unwrap_or_else(|e| {
            eprintln!("failed to read corpus dir {dir}: {e}");
            std::process::exit(2);
        })
        .filter_map(|e| e.ok())
        .collect();
    entries.sort_by_key(|e| e.file_name());

    for entry in entries {
        let path = entry.path();
        if !path.is_file() {
            continue;
        }
        let data = match fs::read(&path) {
            Ok(d) => d,
            Err(_) => continue,
        };
        let mut u = Unstructured::new(&data);
        if let Ok(value) = adapter_fuzz::generate(&mut u) {
            let record = serde_json::json!({
                "file": path.file_name().unwrap().to_string_lossy(),
                "logical_value": to_json(&value),
            });
            if writeln!(out, "{record}").is_err() {
                std::process::exit(1);
            }
        }
    }
}
