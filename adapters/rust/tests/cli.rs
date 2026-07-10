// adapters/rust/tests/cli.rs
use std::io::Write;
use std::process::{Command, Stdio};

fn run_encode(profile: &str, input_lines: &[&str]) -> (i32, String, String) {
    let mut child = Command::new(env!("CARGO_BIN_EXE_adapter"))
        .args(["encode", "--profile", profile])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("failed to spawn adapter binary");

    child
        .stdin
        .take()
        .unwrap()
        .write_all(input_lines.join("\n").as_bytes())
        .unwrap();

    let output = child.wait_with_output().expect("failed to wait on adapter");
    (
        output.status.code().unwrap_or(-1),
        String::from_utf8(output.stdout).unwrap(),
        String::from_utf8(output.stderr).unwrap(),
    )
}

#[test]
fn encodes_two_valid_lines() {
    let (code, stdout, _stderr) = run_encode(
        "rfc8949",
        &[
            r#"{"type": "int", "value": "0"}"#,
            r#"{"type": "int", "value": "23"}"#,
        ],
    );
    assert_eq!(code, 0);
    let lines: Vec<&str> = stdout.trim_end().split('\n').collect();
    assert_eq!(lines, vec!["00", "17"]);
}

#[test]
fn blank_lines_are_ignored() {
    let (code, stdout, _stderr) = run_encode("rfc8949", &[r#"{"type": "int", "value": "0"}"#, "", "   "]);
    assert_eq!(code, 0);
    assert_eq!(stdout.trim_end(), "00");
}

#[test]
fn malformed_line_exits_1_but_still_processes_others() {
    let (code, stdout, stderr) = run_encode(
        "rfc8949",
        &[r#"{"type": "int", "value": "0"}"#, "not json", r#"{"type": "int", "value": "1"}"#],
    );
    assert_eq!(code, 1);
    // One stdout line per input line — the malformed line's output is empty,
    // preserving positional alignment for the harness rather than compacting
    // to success-only output.
    let lines: Vec<&str> = stdout.split('\n').collect();
    assert_eq!(lines, vec!["00", "", "01", ""]);
    assert!(!stderr.is_empty());
}

#[test]
fn unsupported_profile_exits_3() {
    let (code, _stdout, stderr) = run_encode("no-such-profile", &[r#"{"type": "int", "value": "0"}"#]);
    assert_eq!(code, 3);
    assert!(stderr.contains("no-such-profile"));
}

#[test]
fn dcbor_profile_encodes() {
    let (code, stdout, _stderr) = run_encode(
        "dcbor",
        &[r#"{"type": "int", "value": "0"}"#, r#"{"type": "float", "width": "auto", "value": "2.0"}"#],
    );
    assert_eq!(code, 0);
    let lines: Vec<&str> = stdout.trim_end().split('\n').collect();
    assert_eq!(lines, vec!["00", "02"]);
}

fn run_decode_strict(profile: &str, input_lines: &[&str]) -> (i32, String, String) {
    let mut child = Command::new(env!("CARGO_BIN_EXE_adapter"))
        .args(["decode-strict", "--profile", profile])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("failed to spawn adapter binary");

    child
        .stdin
        .take()
        .unwrap()
        .write_all(input_lines.join("\n").as_bytes())
        .unwrap();

    let output = child.wait_with_output().expect("failed to wait on adapter");
    (
        output.status.code().unwrap_or(-1),
        String::from_utf8(output.stdout).unwrap(),
        String::from_utf8(output.stderr).unwrap(),
    )
}

#[test]
fn decode_strict_accepts_canonical_int() {
    let (code, stdout, _stderr) = run_decode_strict("rfc8949", &["00", "17"]);
    assert_eq!(code, 0);
    let lines: Vec<&str> = stdout.trim_end().split('\n').collect();
    assert_eq!(lines, vec!["ACCEPT 00", "ACCEPT 17"]);
}

#[test]
fn decode_strict_rejects_non_shortest_int() {
    // 0x1800 is major type 0 encoded with a redundant one-byte-argument form
    // for a value (0) that fits in the 5-bit direct-argument space.
    let (code, stdout, _stderr) = run_decode_strict("rfc8949", &["1800"]);
    assert_eq!(code, 0);
    assert_eq!(stdout.trim_end(), "REJECT NON_SHORTEST_INT");
}

#[test]
fn decode_strict_mixed_batch_preserves_line_alignment() {
    let (code, stdout, _stderr) = run_decode_strict("rfc8949", &["00", "1800", "17"]);
    assert_eq!(code, 0);
    let lines: Vec<&str> = stdout.trim_end().split('\n').collect();
    assert_eq!(lines, vec!["ACCEPT 00", "REJECT NON_SHORTEST_INT", "ACCEPT 17"]);
}
