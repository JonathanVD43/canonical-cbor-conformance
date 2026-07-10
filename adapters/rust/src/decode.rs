// Strict canonical-CBOR decoder (SPEC.md's `decode-strict` mode). Parses raw
// CBOR bytes into a byte-range-aware intermediate form and rejects any
// well-formed-but-non-canonical input with the single most specific
// decode-strict reason code (P1-P9 / D1-D9). A generic CBOR decode (e.g. via
// the `dcbor` crate's own decoder, or a library's "canonical mode" check)
// normalizes away exactly the information needed to tell *why* something is
// non-canonical: which additional-info width was used, raw NaN payload bits,
// raw map-key byte order. So this parser is hand-rolled instead of wrapping
// an existing decoder.
//
// Known scope gap: a tag-2/3 (bignum) item whose magnitude fits the native
// 64-bit range (and so should have been a plain int, per SPEC.md's bignum
// rule) is not rejected here -- none of the 11 documented decode-strict
// reason codes covers "bignum tag used below native range" and no vector in
// the corpus exercises it. Such input round-trips as a plain tag+bytestring
// passthrough instead.

use unicode_normalization::UnicodeNormalization;

use crate::logical_value::LogicalValue;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Profile {
    Rfc8949,
    Dcbor,
}

// Placeholder allow-list for the UNKNOWN_TAG check: only the bignum tags are
// exercised by the current vector corpus. Grow this if other tags are added.
const ALLOWED_TAGS: &[u64] = &[2, 3];

pub enum Verdict {
    Accept(Vec<u8>),
    Reject(&'static str),
}

const REASON_CODES: &[&str] = &[
    "NON_SHORTEST_INT",
    "UNSORTED_MAP_KEYS",
    "INDEFINITE_LENGTH",
    "DUPLICATE_KEY",
    "NON_SHORTEST_FLOAT",
    "NAN_PAYLOAD_VARIANT",
    "TRAILING_BYTES",
    "MULTIPLE_TOP_LEVEL_ITEMS",
    "UNKNOWN_TAG",
    "NON_NFC_STRING",
    "UNREDUCED_NUMERIC",
];

fn as_reason(err: &str) -> Option<&'static str> {
    REASON_CODES.iter().copied().find(|r| *r == err)
}

pub fn decode_strict(input: &[u8], profile: Profile) -> Result<Verdict, String> {
    if input.is_empty() {
        return Err("internal: empty input line".to_string());
    }
    let mut pos = 0usize;
    let item = match parse_item(input, &mut pos, profile) {
        Ok(item) => item,
        Err(err) => {
            return match as_reason(&err) {
                Some(reason) => Ok(Verdict::Reject(reason)),
                None => Err(err),
            };
        }
    };

    if pos < input.len() {
        let mut pos2 = pos;
        return match parse_item(input, &mut pos2, profile) {
            Ok(_) if pos2 == input.len() => Ok(Verdict::Reject("MULTIPLE_TOP_LEVEL_ITEMS")),
            _ => Ok(Verdict::Reject("TRAILING_BYTES")),
        };
    }

    let logical = item_to_logical(item);
    let encoder: fn(&LogicalValue) -> Result<Vec<u8>, String> = match profile {
        Profile::Rfc8949 => crate::rfc8949::encode,
        Profile::Dcbor => crate::dcbor::encode,
    };
    let reencoded = encoder(&logical).map_err(|e| format!("internal: canonical input failed to re-encode: {e}"))?;
    Ok(Verdict::Accept(reencoded))
}

enum Item {
    UInt(u64),
    NInt(u64),
    Bytes(Vec<u8>),
    Text(String),
    Array(Vec<Item>),
    Map(Vec<(Item, Item)>),
    Tag(u64, Box<Item>),
    Bool(bool),
    Null,
    Float(f64),
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum ArgWidth {
    Direct,
    One,
    Two,
    Four,
    Eight,
}

struct Head {
    major: u8,
    arg: u64,
    width: ArgWidth,
    indefinite: bool,
}

fn read_head(input: &[u8], pos: &mut usize) -> Result<Head, String> {
    let b0 = *input
        .get(*pos)
        .ok_or_else(|| "internal: truncated input (expected an item header)".to_string())?;
    *pos += 1;
    let major = b0 >> 5;
    let info = b0 & 0x1f;
    if info == 31 {
        return Ok(Head { major, arg: 0, width: ArgWidth::Direct, indefinite: true });
    }
    let (arg, width) = match info {
        0..=23 => (info as u64, ArgWidth::Direct),
        24 => {
            let b = *input
                .get(*pos)
                .ok_or_else(|| "internal: truncated 1-byte argument".to_string())?;
            *pos += 1;
            (b as u64, ArgWidth::One)
        }
        25 => {
            let bytes: [u8; 2] = input
                .get(*pos..*pos + 2)
                .ok_or_else(|| "internal: truncated 2-byte argument".to_string())?
                .try_into()
                .unwrap();
            *pos += 2;
            (u16::from_be_bytes(bytes) as u64, ArgWidth::Two)
        }
        26 => {
            let bytes: [u8; 4] = input
                .get(*pos..*pos + 4)
                .ok_or_else(|| "internal: truncated 4-byte argument".to_string())?
                .try_into()
                .unwrap();
            *pos += 4;
            (u32::from_be_bytes(bytes) as u64, ArgWidth::Four)
        }
        27 => {
            let bytes: [u8; 8] = input
                .get(*pos..*pos + 8)
                .ok_or_else(|| "internal: truncated 8-byte argument".to_string())?
                .try_into()
                .unwrap();
            *pos += 8;
            (u64::from_be_bytes(bytes), ArgWidth::Eight)
        }
        _ => return Err("internal: reserved additional-info value (28-30)".to_string()),
    };
    Ok(Head { major, arg, width, indefinite: false })
}

fn shortest_width_for(arg: u64) -> ArgWidth {
    if arg < 24 {
        ArgWidth::Direct
    } else if arg <= 0xff {
        ArgWidth::One
    } else if arg <= 0xffff {
        ArgWidth::Two
    } else if arg <= 0xffff_ffff {
        ArgWidth::Four
    } else {
        ArgWidth::Eight
    }
}

// P1/D2: every integer *argument* (ints themselves, and string/array/map
// lengths, and tag numbers) must use the shortest encoding for its value.
fn check_shortest_arg(head: &Head) -> Result<(), String> {
    if head.width != shortest_width_for(head.arg) {
        return Err("NON_SHORTEST_INT".to_string());
    }
    Ok(())
}

fn parse_item(input: &[u8], pos: &mut usize, profile: Profile) -> Result<Item, String> {
    let head = read_head(input, pos)?;

    if head.major == 7 {
        if head.indefinite {
            return Err("internal: unexpected break byte outside indefinite-length container".to_string());
        }
        return parse_major7(head.width, head.arg, profile);
    }

    if head.indefinite {
        // P3/D1: indefinite-length arrays/maps/byte/text strings are banned
        // outright, regardless of what they'd otherwise contain.
        return Err("INDEFINITE_LENGTH".to_string());
    }
    check_shortest_arg(&head)?;

    match head.major {
        0 => Ok(Item::UInt(head.arg)),
        1 => Ok(Item::NInt(head.arg)),
        2 => {
            let len = head.arg as usize;
            let bytes = input
                .get(*pos..*pos + len)
                .ok_or_else(|| "internal: truncated byte string".to_string())?
                .to_vec();
            *pos += len;
            Ok(Item::Bytes(bytes))
        }
        3 => {
            let len = head.arg as usize;
            let bytes = input
                .get(*pos..*pos + len)
                .ok_or_else(|| "internal: truncated text string".to_string())?;
            let s = std::str::from_utf8(bytes)
                .map_err(|_| "internal: invalid utf-8 in text string".to_string())?
                .to_string();
            *pos += len;
            // D8: dcbor requires NFC-normalized text; rfc8949 has no such
            // rule (P9 is an explicit non-goal).
            if profile == Profile::Dcbor {
                let normalized: String = s.nfc().collect();
                if normalized != s {
                    return Err("NON_NFC_STRING".to_string());
                }
            }
            Ok(Item::Text(s))
        }
        4 => {
            let len = head.arg as usize;
            let mut items = Vec::with_capacity(len);
            for _ in 0..len {
                items.push(parse_item(input, pos, profile)?);
            }
            Ok(Item::Array(items))
        }
        5 => {
            let len = head.arg as usize;
            let mut entries = Vec::with_capacity(len);
            let mut key_ranges: Vec<(usize, usize)> = Vec::with_capacity(len);
            for _ in 0..len {
                let key_start = *pos;
                let key = parse_item(input, pos, profile)?;
                let key_end = *pos;
                let value = parse_item(input, pos, profile)?;
                key_ranges.push((key_start, key_end));
                entries.push((key, value));
            }
            // P2/D3: keys must appear in strictly increasing bytewise order
            // of their own raw encoded bytes. A duplicate key is necessarily
            // adjacent to itself in properly sorted order, so one adjacent
            // pass catches both violations.
            for w in key_ranges.windows(2) {
                let a = &input[w[0].0..w[0].1];
                let b = &input[w[1].0..w[1].1];
                if a == b {
                    return Err("DUPLICATE_KEY".to_string());
                }
                if a > b {
                    return Err("UNSORTED_MAP_KEYS".to_string());
                }
            }
            Ok(Item::Map(entries))
        }
        6 => {
            let tag = head.arg;
            if !ALLOWED_TAGS.contains(&tag) {
                return Err("UNKNOWN_TAG".to_string());
            }
            let inner = parse_item(input, pos, profile)?;
            Ok(Item::Tag(tag, Box::new(inner)))
        }
        _ => unreachable!("major type is 3 bits, always 0-7"),
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum FloatWidth {
    F16,
    F32,
    F64,
}

fn parse_major7(width: ArgWidth, arg: u64, profile: Profile) -> Result<Item, String> {
    match width {
        ArgWidth::Direct => match arg {
            20 => Ok(Item::Bool(false)),
            21 => Ok(Item::Bool(true)),
            22 => Ok(Item::Null),
            other => Err(format!("internal: unsupported major-7 simple value {other}")),
        },
        ArgWidth::One => Err("internal: unsupported major-7 1-byte simple value".to_string()),
        ArgWidth::Two => check_float(arg, FloatWidth::F16, profile),
        ArgWidth::Four => check_float(arg, FloatWidth::F32, profile),
        ArgWidth::Eight => check_float(arg, FloatWidth::F64, profile),
    }
}

fn check_float(bits: u64, width: FloatWidth, profile: Profile) -> Result<Item, String> {
    let value = match width {
        FloatWidth::F16 => half::f16::from_bits(bits as u16).to_f64(),
        FloatWidth::F32 => f32::from_bits(bits as u32) as f64,
        FloatWidth::F64 => f64::from_bits(bits),
    };

    if value.is_nan() {
        // P6/D6: canonical NaN is exactly f16 with payload 0x7e00, in both profiles.
        if width != FloatWidth::F16 || bits != 0x7e00 {
            return Err("NAN_PAYLOAD_VARIANT".to_string());
        }
        return Ok(Item::Float(f64::NAN));
    }

    // D5/D7 (dcbor only): any whole-number float in [-2^63, 2^64-1] -
    // including +/-0.0, per D7 zero unification - must be a plain int
    // instead. This is checked before, and takes priority over, the general
    // shortest-float-width rule below.
    if profile == Profile::Dcbor && is_dcbor_reducible(value) {
        return Err("UNREDUCED_NUMERIC".to_string());
    }

    // P5/D2: the width used must be the narrowest of f16/f32/f64 that
    // round-trips `value` exactly.
    let shortest = shortest_float_width(value);
    if width != shortest {
        return Err("NON_SHORTEST_FLOAT".to_string());
    }

    Ok(Item::Float(value))
}

fn is_dcbor_reducible(value: f64) -> bool {
    if value.fract() != 0.0 {
        return false;
    }
    value >= -9_223_372_036_854_775_808.0 && value <= 18_446_744_073_709_551_615.0
}

fn shortest_float_width(value: f64) -> FloatWidth {
    let h = half::f16::from_f64(value);
    if h.to_f64().to_bits() == value.to_bits() {
        return FloatWidth::F16;
    }
    let f = value as f32;
    if (f as f64).to_bits() == value.to_bits() {
        return FloatWidth::F32;
    }
    FloatWidth::F64
}

fn format_float(value: f64) -> String {
    if value.is_nan() {
        "NaN".to_string()
    } else {
        format!("{value}")
    }
}

fn item_to_logical(item: Item) -> LogicalValue {
    match item {
        Item::UInt(v) => LogicalValue::Int(v.to_string()),
        Item::NInt(v) => LogicalValue::Int((-1i128 - v as i128).to_string()),
        Item::Bytes(b) => LogicalValue::Bytes(crate::util::hex_encode(&b)),
        Item::Text(s) => LogicalValue::Text(s),
        Item::Array(items) => LogicalValue::Array(items.into_iter().map(item_to_logical).collect()),
        Item::Map(entries) => {
            LogicalValue::Map(entries.into_iter().map(|(k, v)| (item_to_logical(k), item_to_logical(v))).collect())
        }
        Item::Tag(tag, inner) => LogicalValue::Tag(tag, Box::new(item_to_logical(*inner))),
        Item::Bool(b) => LogicalValue::Bool(b),
        Item::Null => LogicalValue::Null,
        Item::Float(f) => LogicalValue::Float { width: "auto".to_string(), value: format_float(f) },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::hex_decode;

    fn accept_hex(hex: &str, profile: Profile) -> Vec<u8> {
        match decode_strict(&hex_decode(hex).unwrap(), profile).unwrap() {
            Verdict::Accept(bytes) => bytes,
            Verdict::Reject(reason) => panic!("expected ACCEPT, got REJECT {reason}"),
        }
    }

    fn reject_hex(hex: &str, profile: Profile) -> &'static str {
        match decode_strict(&hex_decode(hex).unwrap(), profile).unwrap() {
            Verdict::Accept(bytes) => panic!("expected REJECT, got ACCEPT {}", crate::util::hex_encode(&bytes)),
            Verdict::Reject(reason) => reason,
        }
    }

    #[test]
    fn accepts_canonical_int_and_round_trips() {
        assert_eq!(accept_hex("00", Profile::Rfc8949), hex_decode("00").unwrap());
        assert_eq!(accept_hex("1818", Profile::Rfc8949), hex_decode("1818").unwrap());
    }

    #[test]
    fn accepts_canonical_map_and_array() {
        assert_eq!(accept_hex("a1616101", Profile::Rfc8949), hex_decode("a1616101").unwrap());
        assert_eq!(accept_hex("820102", Profile::Rfc8949), hex_decode("820102").unwrap());
    }

    // Hex literals below are copied verbatim from
    // vectors/v1/cbor/{rfc8949,dcbor}/hand-written/strict-decode-reject/*.json.

    #[test]
    fn non_shortest_int() {
        assert_eq!(reject_hex("1800", Profile::Rfc8949), "NON_SHORTEST_INT");
        assert_eq!(reject_hex("1800", Profile::Dcbor), "NON_SHORTEST_INT");
    }

    #[test]
    fn unsorted_map_keys() {
        assert_eq!(reject_hex("a201000000", Profile::Rfc8949), "UNSORTED_MAP_KEYS");
        assert_eq!(reject_hex("a201000000", Profile::Dcbor), "UNSORTED_MAP_KEYS");
    }

    #[test]
    fn indefinite_length() {
        assert_eq!(reject_hex("9f00ff", Profile::Rfc8949), "INDEFINITE_LENGTH");
        assert_eq!(reject_hex("9f00ff", Profile::Dcbor), "INDEFINITE_LENGTH");
    }

    #[test]
    fn duplicate_key() {
        assert_eq!(reject_hex("a200000001", Profile::Rfc8949), "DUPLICATE_KEY");
        assert_eq!(reject_hex("a200000001", Profile::Dcbor), "DUPLICATE_KEY");
    }

    #[test]
    fn non_shortest_float() {
        assert_eq!(reject_hex("fb4004000000000000", Profile::Rfc8949), "NON_SHORTEST_FLOAT");
        assert_eq!(reject_hex("fb4004000000000000", Profile::Dcbor), "NON_SHORTEST_FLOAT");
    }

    #[test]
    fn nan_payload_variant() {
        assert_eq!(reject_hex("f97e01", Profile::Rfc8949), "NAN_PAYLOAD_VARIANT");
        assert_eq!(reject_hex("f97e01", Profile::Dcbor), "NAN_PAYLOAD_VARIANT");
    }

    #[test]
    fn trailing_bytes() {
        assert_eq!(reject_hex("0018", Profile::Rfc8949), "TRAILING_BYTES");
        assert_eq!(reject_hex("0018", Profile::Dcbor), "TRAILING_BYTES");
    }

    #[test]
    fn multiple_top_level_items() {
        assert_eq!(reject_hex("0001", Profile::Rfc8949), "MULTIPLE_TOP_LEVEL_ITEMS");
        assert_eq!(reject_hex("0001", Profile::Dcbor), "MULTIPLE_TOP_LEVEL_ITEMS");
    }

    #[test]
    fn unknown_tag() {
        assert_eq!(reject_hex("d903e700", Profile::Rfc8949), "UNKNOWN_TAG");
        assert_eq!(reject_hex("d903e700", Profile::Dcbor), "UNKNOWN_TAG");
    }

    #[test]
    fn non_nfc_string_dcbor_only() {
        assert_eq!(reject_hex("6663616665cc81", Profile::Dcbor), "NON_NFC_STRING");
        // rfc8949 has no NFC rule (P9): the same bytes must be accepted.
        assert_eq!(accept_hex("6663616665cc81", Profile::Rfc8949), hex_decode("6663616665cc81").unwrap());
    }

    #[test]
    fn unreduced_numeric_dcbor_only() {
        assert_eq!(reject_hex("f94000", Profile::Dcbor), "UNREDUCED_NUMERIC");
        // rfc8949 has no reduction rule: a shortest-form whole float is fine.
        assert_eq!(accept_hex("f94000", Profile::Rfc8949), hex_decode("f94000").unwrap());
    }
}
