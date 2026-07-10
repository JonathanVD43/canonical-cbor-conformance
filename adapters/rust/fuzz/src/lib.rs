// Shared coverage-guided generator for both fuzz targets (fuzz_targets/rfc8949.rs,
// fuzz_targets/dcbor.rs) and the offline replay tool (src/bin/replay.rs). Both call
// `generate` with an `arbitrary::Unstructured` built from the same raw bytes, so a
// libFuzzer corpus file always reconstructs to the identical LogicalValue tree.
use adapter::logical_value::LogicalValue;
use arbitrary::{Result, Unstructured};

const MAX_DEPTH: u32 = 4;
const MAX_COLLECTION_LEN: usize = 6;

const INT_BOUNDARIES: &[&str] = &[
    "0", "1", "-1", "23", "24", "-24", "-25", "255", "256", "-256", "-257", "65535", "65536",
    "-65536", "-65537", "4294967295", "4294967296", "-4294967296", "-4294967297",
    "9223372036854775807", "-9223372036854775808", "18446744073709551615",
    "-18446744073709551616", "18446744073709551616", "-18446744073709551617",
];

const NFC_TEXT_SAMPLES: &[&str] = &[
    "cafe\u{0301}",       // "café" NFC-decomposed (e + combining acute)
    "n\u{0303}",          // combining tilde alone
    "e\u{0301}\u{0327}",  // stacked combining marks
    "\u{1E9B}\u{0323}",   // pre-composed + combining, forces re-normalization
];

pub fn generate(u: &mut Unstructured) -> Result<LogicalValue> {
    gen_value(u, MAX_DEPTH)
}

fn gen_value(u: &mut Unstructured, depth: u32) -> Result<LogicalValue> {
    let max_variant = if depth == 0 { 5 } else { 9 };
    match u.int_in_range(0..=max_variant)? {
        0 => gen_int(u),
        1 => gen_float(u),
        2 => gen_text(u),
        3 => gen_bytes(u),
        4 => Ok(LogicalValue::Bool(u.arbitrary()?)),
        5 => Ok(LogicalValue::Null),
        6 => gen_array(u, depth),
        7 => gen_map(u, depth),
        8 => gen_tag(u, depth),
        _ => gen_bignum(u),
    }
}

fn gen_int(u: &mut Unstructured) -> Result<LogicalValue> {
    if u.ratio(1, 2)? {
        let idx = u.choose_index(INT_BOUNDARIES.len())?;
        Ok(LogicalValue::Int(INT_BOUNDARIES[idx].to_string()))
    } else {
        let n: i128 = u.arbitrary()?;
        Ok(LogicalValue::Int(n.to_string()))
    }
}

fn format_float(f: f64) -> String {
    if f.is_nan() {
        "NaN".to_string()
    } else if f.is_infinite() {
        if f > 0.0 { "Infinity".to_string() } else { "-Infinity".to_string() }
    } else {
        format!("{f}")
    }
}

fn gen_float(u: &mut Unstructured) -> Result<LogicalValue> {
    // width is deliberately always "auto": explicit-width forcing (and its
    // precision-loss rejection) is already covered by the hand-written
    // corpus, so the fuzz generator stays in the always-valid "auto" lane.
    let value = match u.int_in_range(0..=6)? {
        0 => "NaN".to_string(),
        1 => "Infinity".to_string(),
        2 => "-Infinity".to_string(),
        3 => "0.0".to_string(),
        4 => "-0.0".to_string(),
        5 => {
            let bits: u64 = u.arbitrary()?;
            format_float(f64::from_bits(bits))
        }
        _ => {
            let f: f64 = u.arbitrary()?;
            format_float(f)
        }
    };
    Ok(LogicalValue::Float { width: "auto".to_string(), value })
}

fn gen_text(u: &mut Unstructured) -> Result<LogicalValue> {
    let s = if u.ratio(1, 3)? {
        let idx = u.choose_index(NFC_TEXT_SAMPLES.len())?;
        NFC_TEXT_SAMPLES[idx].to_string()
    } else {
        u.arbitrary::<String>()?
    };
    Ok(LogicalValue::Text(s))
}

fn gen_bytes(u: &mut Unstructured) -> Result<LogicalValue> {
    let len = u.int_in_range(0..=32)?;
    let mut bytes = Vec::with_capacity(len);
    for _ in 0..len {
        bytes.push(u.arbitrary::<u8>()?);
    }
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    Ok(LogicalValue::Bytes(hex))
}

fn gen_array(u: &mut Unstructured, depth: u32) -> Result<LogicalValue> {
    let len = u.int_in_range(0..=MAX_COLLECTION_LEN)?;
    let mut items = Vec::with_capacity(len);
    for _ in 0..len {
        items.push(gen_value(u, depth - 1)?);
    }
    Ok(LogicalValue::Array(items))
}

fn gen_map(u: &mut Unstructured, depth: u32) -> Result<LogicalValue> {
    let len = u.int_in_range(0..=MAX_COLLECTION_LEN)?;
    let mut entries = Vec::with_capacity(len);
    for _ in 0..len {
        entries.push((gen_value(u, depth - 1)?, gen_value(u, depth - 1)?));
    }
    Ok(LogicalValue::Map(entries))
}

fn gen_tag(u: &mut Unstructured, depth: u32) -> Result<LogicalValue> {
    let tag: u64 = if u.ratio(1, 4)? {
        *u.choose(&[0u64, 1, 2, 3, 100, 1000, u64::MAX])?
    } else {
        u.arbitrary()?
    };
    let inner = gen_value(u, depth.saturating_sub(1))?;
    Ok(LogicalValue::Tag(tag, Box::new(inner)))
}

fn gen_bignum(u: &mut Unstructured) -> Result<LogicalValue> {
    let sign = if u.ratio(1, 2)? { "positive" } else { "negative" };
    let two_pow_64: u128 = 1u128 << 64;
    // 90% valid (>= 2^64, the only magnitude a bignum is allowed to encode);
    // 10% deliberately below the floor to exercise the encoder's rejection path.
    let magnitude: u128 = if u.ratio(1, 10)? {
        u.int_in_range(0..=(two_pow_64 - 1))?
    } else {
        let extra: u128 = u.arbitrary()?;
        two_pow_64.saturating_add(extra % (u128::MAX - two_pow_64))
    };
    Ok(LogicalValue::Bignum { sign: sign.to_string(), value: magnitude.to_string() })
}
