use adapter::{dcbor, decode, logical_value, rfc8949, util};

use std::io::{self, BufRead, Write};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: adapter <mode> --profile <profile>");
        std::process::exit(2);
    }
    let profile = args
        .iter()
        .position(|a| a == "--profile")
        .and_then(|i| args.get(i + 1))
        .map(String::as_str);
    match args[1].as_str() {
        "encode" => {
            std::process::exit(run_encode(profile));
        }
        "decode-strict" => {
            std::process::exit(run_decode_strict(profile));
        }
        other => {
            eprintln!("unknown mode: {other}");
            std::process::exit(2);
        }
    }
}

fn run_encode(profile: Option<&str>) -> i32 {
    let encoder: fn(&logical_value::LogicalValue) -> Result<Vec<u8>, String> = match profile {
        Some("rfc8949") => rfc8949::encode,
        Some("dcbor") => dcbor::encode,
        Some(other) => {
            eprintln!("unsupported profile: {other}");
            return 3;
        }
        None => {
            eprintln!("--profile is required");
            return 2;
        }
    };

    let stdin = io::stdin();
    let stdout = io::stdout();
    let mut out = stdout.lock();
    let mut had_error = false;

    for line in stdin.lock().lines() {
        let line = match line {
            Ok(l) => l,
            Err(e) => {
                eprintln!("internal adapter error: failed to read stdin: {e}");
                std::process::exit(2);
            }
        };
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        let json: serde_json::Value = match serde_json::from_str(trimmed) {
            Ok(v) => v,
            Err(e) => {
                eprintln!("malformed input line: {e}");
                had_error = true;
                if let Err(e) = writeln!(out) {
                    eprintln!("internal adapter error: failed to write stdout: {e}");
                    std::process::exit(2);
                }
                continue;
            }
        };
        let logical = match logical_value::parse(&json) {
            Ok(v) => v,
            Err(e) => {
                eprintln!("malformed input line: {}", e.0);
                had_error = true;
                if let Err(e) = writeln!(out) {
                    eprintln!("internal adapter error: failed to write stdout: {e}");
                    std::process::exit(2);
                }
                continue;
            }
        };
        match encoder(&logical) {
            Ok(bytes) => {
                if let Err(e) = writeln!(out, "{}", util::hex_encode(&bytes)) {
                    eprintln!("internal adapter error: failed to write stdout: {e}");
                    std::process::exit(2);
                }
            }
            Err(e) => {
                eprintln!("encode rejected: {e}");
                had_error = true;
                if let Err(e) = writeln!(out) {
                    eprintln!("internal adapter error: failed to write stdout: {e}");
                    std::process::exit(2);
                }
            }
        }
    }

    if had_error {
        1
    } else {
        0
    }
}

fn run_decode_strict(profile: Option<&str>) -> i32 {
    let decode_profile = match profile {
        Some("rfc8949") => decode::Profile::Rfc8949,
        Some("dcbor") => decode::Profile::Dcbor,
        Some(other) => {
            eprintln!("unsupported profile: {other}");
            return 3;
        }
        None => {
            eprintln!("--profile is required");
            return 2;
        }
    };

    let stdin = io::stdin();
    let stdout = io::stdout();
    let mut out = stdout.lock();
    let mut had_error = false;

    for line in stdin.lock().lines() {
        let line = match line {
            Ok(l) => l,
            Err(e) => {
                eprintln!("internal adapter error: failed to read stdin: {e}");
                std::process::exit(2);
            }
        };
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        let bytes = match util::hex_decode(trimmed) {
            Ok(b) => b,
            Err(e) => {
                eprintln!("malformed input line: {e}");
                had_error = true;
                if let Err(e) = writeln!(out) {
                    eprintln!("internal adapter error: failed to write stdout: {e}");
                    std::process::exit(2);
                }
                continue;
            }
        };
        match decode::decode_strict(&bytes, decode_profile) {
            Ok(decode::Verdict::Accept(reencoded)) => {
                if let Err(e) = writeln!(out, "ACCEPT {}", util::hex_encode(&reencoded)) {
                    eprintln!("internal adapter error: failed to write stdout: {e}");
                    std::process::exit(2);
                }
            }
            Ok(decode::Verdict::Reject(reason)) => {
                if let Err(e) = writeln!(out, "REJECT {reason}") {
                    eprintln!("internal adapter error: failed to write stdout: {e}");
                    std::process::exit(2);
                }
            }
            Err(e) => {
                eprintln!("decode-strict internal error: {e}");
                had_error = true;
                if let Err(e) = writeln!(out) {
                    eprintln!("internal adapter error: failed to write stdout: {e}");
                    std::process::exit(2);
                }
            }
        }
    }

    if had_error {
        1
    } else {
        0
    }
}
