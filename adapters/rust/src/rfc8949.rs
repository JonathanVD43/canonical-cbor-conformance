use crate::logical_value::LogicalValue;
use crate::util::hex_decode;

fn same_bits(a: f64, b: f64) -> bool {
    a.to_bits() == b.to_bits()
}

// Callers must handle NaN before reaching these helpers (see encode_float's
// early return) — same_bits's to_bits comparison never matches NaN to NaN.
fn try_f16(f: f64) -> Option<[u8; 2]> {
    let h = half::f16::from_f64(f);
    let back = h.to_f64();
    if same_bits(back, f) {
        return Some(h.to_bits().to_be_bytes());
    }
    None
}

fn try_f32(f: f64) -> Option<[u8; 4]> {
    let v = f as f32;
    let back = v as f64;
    if same_bits(back, f) {
        return Some(v.to_be_bytes());
    }
    None
}

fn parse_float_literal(literal: &str) -> Result<f64, String> {
    literal
        .parse::<f64>()
        .map_err(|e| format!("float: invalid literal {literal:?}: {e}"))
}

fn encode_float(width: &str, literal: &str) -> Result<Vec<u8>, String> {
    let f = parse_float_literal(literal)?;
    if f.is_nan() {
        // P6: NaN always uses the single canonical f16 payload, regardless of width.
        return Ok(vec![0xf9, 0x7e, 0x00]);
    }
    if width == "auto" {
        if let Some(b) = try_f16(f) {
            let mut v = vec![0xf9];
            v.extend_from_slice(&b);
            return Ok(v);
        }
        if let Some(b) = try_f32(f) {
            let mut v = vec![0xfa];
            v.extend_from_slice(&b);
            return Ok(v);
        }
        let mut v = vec![0xfb];
        v.extend_from_slice(&f.to_be_bytes());
        return Ok(v);
    }
    match width {
        "f16" => {
            let b = try_f16(f).ok_or_else(|| format!("float: {f} cannot round-trip at requested width f16"))?;
            let mut v = vec![0xf9];
            v.extend_from_slice(&b);
            Ok(v)
        }
        "f32" => {
            let b = try_f32(f).ok_or_else(|| format!("float: {f} cannot round-trip at requested width f32"))?;
            let mut v = vec![0xfa];
            v.extend_from_slice(&b);
            Ok(v)
        }
        "f64" => {
            let mut v = vec![0xfb];
            v.extend_from_slice(&f.to_be_bytes());
            Ok(v)
        }
        other => Err(format!("float: unknown width {other:?}")),
    }
}

fn encode_head(major_type: u8, arg: u64) -> Vec<u8> {
    let mt = major_type << 5;
    if arg < 24 {
        vec![mt | (arg as u8)]
    } else if arg <= 0xff {
        vec![mt | 24, arg as u8]
    } else if arg <= 0xffff {
        let mut v = vec![mt | 25];
        v.extend_from_slice(&(arg as u16).to_be_bytes());
        v
    } else if arg <= 0xffff_ffff {
        let mut v = vec![mt | 26];
        v.extend_from_slice(&(arg as u32).to_be_bytes());
        v
    } else {
        let mut v = vec![mt | 27];
        v.extend_from_slice(&arg.to_be_bytes());
        v
    }
}

fn encode_int(value: &str) -> Result<Vec<u8>, String> {
    let n: i128 = value.parse().map_err(|e| format!("int: invalid integer literal {value:?}: {e}"))?;
    if n >= 0 {
        let arg: u64 = n.try_into().map_err(|_| format!("int: {n} exceeds native range"))?;
        Ok(encode_head(0, arg))
    } else {
        let arg: u64 = (-1i128 - n).try_into().map_err(|_| format!("int: {n} exceeds native range"))?;
        Ok(encode_head(1, arg))
    }
}

// Shared with the dcbor profile: bignum tag+magnitude rules are identical in
// both profiles (no dcbor-specific bignum rule exists), so both encoders
// build on this single validated (tag, magnitude-bytes) computation.
pub(crate) fn bignum_tag_and_bytes(sign: &str, value: &str) -> Result<(u8, Vec<u8>), String> {
    let magnitude: u128 = value
        .parse()
        .map_err(|e| format!("bignum: invalid magnitude {value:?}: {e}"))?;
    let two_pow_64: u128 = 1u128 << 64;
    if magnitude < two_pow_64 {
        let tag = if sign == "positive" { 2 } else { 3 };
        return Err(format!(
            "bignum magnitude {magnitude} fits in a native CBOR integer and must not be encoded as tag {tag} (SPEC.md bignum rule)"
        ));
    }
    let (raw_int, tag): (u128, u8) = match sign {
        "positive" => (magnitude, 2),
        "negative" => (magnitude - 1, 3),
        other => return Err(format!("bignum: unknown sign {other:?}")),
    };
    let mut bytes = raw_int.to_be_bytes().to_vec();
    while bytes.len() > 1 && bytes[0] == 0 {
        bytes.remove(0);
    }
    Ok((tag, bytes))
}

fn encode_bignum(sign: &str, value: &str) -> Result<Vec<u8>, String> {
    let (tag, bytes) = bignum_tag_and_bytes(sign, value)?;
    let mut out = encode_head(6, tag as u64);
    out.extend(encode_head(2, bytes.len() as u64));
    out.extend(bytes);
    Ok(out)
}

pub fn encode(value: &LogicalValue) -> Result<Vec<u8>, String> {
    match value {
        LogicalValue::Int(s) => encode_int(s),
        LogicalValue::Text(s) => {
            let bytes = s.as_bytes();
            let mut out = encode_head(3, bytes.len() as u64);
            out.extend_from_slice(bytes);
            Ok(out)
        }
        LogicalValue::Bytes(hex) => {
            let bytes = hex_decode(hex)?;
            let mut out = encode_head(2, bytes.len() as u64);
            out.extend_from_slice(&bytes);
            Ok(out)
        }
        LogicalValue::Bool(b) => Ok(vec![if *b { 0xf5 } else { 0xf4 }]),
        LogicalValue::Null => Ok(vec![0xf6]),
        LogicalValue::Array(items) => {
            let mut out = encode_head(4, items.len() as u64);
            for item in items {
                out.extend(encode(item)?);
            }
            Ok(out)
        }
        LogicalValue::Float { width, value } => encode_float(width, value),
        LogicalValue::Map(entries) => {
            let mut encoded: Vec<(Vec<u8>, Vec<u8>)> = Vec::with_capacity(entries.len());
            for (k, v) in entries {
                encoded.push((encode(k)?, encode(v)?));
            }
            encoded.sort_by(|a, b| a.0.cmp(&b.0));
            let mut out = encode_head(5, encoded.len() as u64);
            for (k, v) in encoded {
                out.extend(k);
                out.extend(v);
            }
            Ok(out)
        }
        LogicalValue::Tag(tag, inner) => {
            let mut out = encode_head(6, *tag);
            out.extend(encode(inner)?);
            Ok(out)
        }
        LogicalValue::Bignum { sign, value } => encode_bignum(sign, value),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::logical_value::LogicalValue as LV;

    #[test]
    fn shortest_int_zero() {
        assert_eq!(encode(&LV::Int("0".to_string())).unwrap(), vec![0x00]);
    }

    #[test]
    fn shortest_int_boundary_23_24() {
        assert_eq!(encode(&LV::Int("23".to_string())).unwrap(), vec![0x17]);
        assert_eq!(encode(&LV::Int("24".to_string())).unwrap(), vec![0x18, 0x18]);
    }

    #[test]
    fn shortest_int_boundary_255_256() {
        assert_eq!(encode(&LV::Int("255".to_string())).unwrap(), vec![0x18, 0xff]);
        assert_eq!(encode(&LV::Int("256".to_string())).unwrap(), vec![0x19, 0x01, 0x00]);
    }

    #[test]
    fn shortest_int_boundary_65535_65536() {
        assert_eq!(encode(&LV::Int("65535".to_string())).unwrap(), vec![0x19, 0xff, 0xff]);
        assert_eq!(encode(&LV::Int("65536".to_string())).unwrap(), vec![0x1a, 0x00, 0x01, 0x00, 0x00]);
    }

    #[test]
    fn shortest_int_boundary_4294967295_4294967296() {
        assert_eq!(encode(&LV::Int("4294967295".to_string())).unwrap(), vec![0x1a, 0xff, 0xff, 0xff, 0xff]);
        assert_eq!(
            encode(&LV::Int("4294967296".to_string())).unwrap(),
            vec![0x1b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00]
        );
    }

    #[test]
    fn negative_int() {
        assert_eq!(encode(&LV::Int("-1".to_string())).unwrap(), vec![0x20]);
    }

    #[test]
    fn negative_int_min_i64_boundary() {
        assert_eq!(
            encode(&LV::Int("-9223372036854775808".to_string())).unwrap(),
            hex_decode("3b7fffffffffffffff").unwrap()
        );
    }

    #[test]
    fn text_and_bytes() {
        assert_eq!(encode(&LV::Text("a".to_string())).unwrap(), vec![0x61, b'a']);
        assert_eq!(encode(&LV::Bytes("deadbeef".to_string())).unwrap(), vec![0x44, 0xde, 0xad, 0xbe, 0xef]);
    }

    #[test]
    fn bool_and_null() {
        assert_eq!(encode(&LV::Bool(true)).unwrap(), vec![0xf5]);
        assert_eq!(encode(&LV::Bool(false)).unwrap(), vec![0xf4]);
        assert_eq!(encode(&LV::Null).unwrap(), vec![0xf6]);
    }

    #[test]
    fn array_of_ints() {
        let arr = LV::Array(vec![LV::Int("1".to_string()), LV::Int("2".to_string())]);
        assert_eq!(encode(&arr).unwrap(), vec![0x82, 0x01, 0x02]);
    }

    #[test]
    fn nan_canonical_payload() {
        let v = LV::Float { width: "auto".to_string(), value: "NaN".to_string() };
        assert_eq!(encode(&v).unwrap(), hex_decode("f97e00").unwrap());
    }

    #[test]
    fn nan_canonical_payload_under_explicit_width_forcing() {
        // P6: NaN always uses the canonical f16 payload, even when an
        // explicit non-f16 width is requested.
        for width in ["f16", "f32", "f64"] {
            let v = LV::Float { width: width.to_string(), value: "NaN".to_string() };
            assert_eq!(encode(&v).unwrap(), hex_decode("f97e00").unwrap());
        }
    }

    #[test]
    fn negative_zero_preserved_distinct_from_zero() {
        let neg = LV::Float { width: "auto".to_string(), value: "-0.0".to_string() };
        let pos = LV::Float { width: "auto".to_string(), value: "0.0".to_string() };
        let neg_bytes = encode(&neg).unwrap();
        let pos_bytes = encode(&pos).unwrap();
        assert_eq!(neg_bytes, hex_decode("f98000").unwrap());
        assert_eq!(pos_bytes, hex_decode("f90000").unwrap());
        assert_ne!(neg_bytes, pos_bytes);
    }

    #[test]
    fn shortest_float_form_f16_exact() {
        let v = LV::Float { width: "auto".to_string(), value: "2.5".to_string() };
        assert_eq!(encode(&v).unwrap(), hex_decode("f94100").unwrap());
    }

    #[test]
    fn explicit_float_width_forces_encoding() {
        // 2.5's shortest-auto form is f16 (f94100), but width="f64" must force the 8-byte double form.
        let v = LV::Float { width: "f64".to_string(), value: "2.5".to_string() };
        assert_eq!(encode(&v).unwrap(), hex_decode("fb4004000000000000").unwrap());
    }

    #[test]
    fn explicit_float_width_raises_on_precision_loss() {
        // 0.1 has no exact f16 representation; forcing width="f16" must error, not silently truncate.
        let v = LV::Float { width: "f16".to_string(), value: "0.1".to_string() };
        assert!(encode(&v).is_err());
    }

    #[test]
    fn map_keys_sorted_by_encoded_bytes() {
        // encoded key for 9 is 0x09 (1 byte), for 10 is 0x0a (1 byte) -> 0x09 < 0x0a, so 9 sorts first.
        // even though this happens to match numeric order, the point is P2 sorts by bytes, not value.
        let value = LV::Map(vec![
            (LV::Int("9".to_string()), LV::Int("1".to_string())),
            (LV::Int("10".to_string()), LV::Int("2".to_string())),
        ]);
        assert_eq!(encode(&value).unwrap(), hex_decode("a209010a02").unwrap());
    }

    #[test]
    fn map_keys_pure_bytewise_no_length_presort() {
        // -24 encodes to 1 byte (0x37); 1000 encodes to 3 bytes (0x1903e8). RFC 8949 SS4.2.1
        // requires PURE bytewise order with no length pre-sort: 0x19 < 0x37, so the 3-byte
        // encoding of 1000 must sort before the 1-byte encoding of -24.
        let value = LV::Map(vec![
            (LV::Int("-24".to_string()), LV::Int("1".to_string())),
            (LV::Int("1000".to_string()), LV::Int("2".to_string())),
        ]);
        assert_eq!(encode(&value).unwrap(), hex_decode("a21903e8023701").unwrap());
    }

    #[test]
    fn bignum_at_2_pow_64_uses_tag_2() {
        let value = LV::Bignum { sign: "positive".to_string(), value: "18446744073709551616".to_string() };
        assert_eq!(encode(&value).unwrap(), hex_decode("c249010000000000000000").unwrap());
    }

    #[test]
    fn negative_bignum_offset_by_one() {
        // RFC 8949 SS3.4.3: tag 3's payload encodes n = magnitude - 1. For magnitude 2^64,
        // n = 2^64 - 1, whose minimal big-endian encoding is 8 bytes of 0xff (not 9 bytes with
        // a leading 0x01).
        let value = LV::Bignum { sign: "negative".to_string(), value: "18446744073709551616".to_string() };
        assert_eq!(encode(&value).unwrap(), hex_decode("c348ffffffffffffffff").unwrap());
    }

    #[test]
    fn bignum_below_2_64_is_rejected() {
        let positive = LV::Bignum { sign: "positive".to_string(), value: "5".to_string() };
        assert!(encode(&positive).is_err());

        let negative = LV::Bignum { sign: "negative".to_string(), value: "9223372036854775808".to_string() };
        assert!(encode(&negative).is_err());
    }

    #[test]
    fn tag_wraps_inner_value() {
        let value = LV::Tag(100, Box::new(LV::Int("5".to_string())));
        assert_eq!(encode(&value).unwrap(), vec![0xd8, 0x64, 0x05]);
    }
}
