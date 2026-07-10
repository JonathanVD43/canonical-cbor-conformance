// dCBOR profile: wraps the `dcbor` crate (Blockchain Commons, v0.25) rather
// than reimplementing deterministic-encoding rules from scratch. The crate's
// `to_cbor_data()` already gives us D5 (numeric reduction), D6 (NaN canonical
// payload), D7 (zero unification), and D8 (NFC normalization) for free —
// confirmed against every committed hand-written vector before writing this
// file (see the Task 9 spike). Bignums have no dcbor-specific rule, so they
// reuse rfc8949's validated (tag, magnitude-bytes) computation directly.
use dcbor::prelude::*;

use crate::logical_value::LogicalValue;
use crate::rfc8949::bignum_tag_and_bytes;
use crate::util::hex_decode;

fn encode_int(value: &str) -> Result<CBOR, String> {
    let n: i128 = value.parse().map_err(|e| format!("int: invalid integer literal {value:?}: {e}"))?;
    if n >= 0 {
        let arg: u64 = n.try_into().map_err(|_| format!("int: {n} exceeds native range"))?;
        Ok(CBORCase::Unsigned(arg).into())
    } else {
        let arg: u64 = (-1i128 - n).try_into().map_err(|_| format!("int: {n} exceeds native range"))?;
        Ok(CBORCase::Negative(arg).into())
    }
}

fn encode_bignum(sign: &str, value: &str) -> Result<CBOR, String> {
    let (tag, bytes) = bignum_tag_and_bytes(sign, value)?;
    Ok(CBOR::to_tagged_value(tag as u64, ByteString::from(bytes)))
}

fn to_cbor(value: &LogicalValue) -> Result<CBOR, String> {
    match value {
        LogicalValue::Int(s) => encode_int(s),
        LogicalValue::Float { value, .. } => {
            // dCBOR always reduces to the canonical numeric form (D5/D6/D7)
            // regardless of any requested width — there is no "explicit
            // width forcing" concept in deterministic CBOR.
            let f: f64 = value
                .parse()
                .map_err(|e| format!("float: invalid literal {value:?}: {e}"))?;
            Ok(CBOR::from(f))
        }
        LogicalValue::Text(s) => Ok(CBOR::from(s.as_str())),
        LogicalValue::Bytes(hex) => {
            let bytes = hex_decode(hex)?;
            Ok(CBOR::from(ByteString::from(bytes)))
        }
        LogicalValue::Bool(b) => Ok(CBOR::from(*b)),
        LogicalValue::Null => Ok(CBOR::null()),
        LogicalValue::Array(items) => {
            let mut arr = Vec::with_capacity(items.len());
            for item in items {
                arr.push(to_cbor(item)?);
            }
            Ok(CBOR::from(arr))
        }
        LogicalValue::Map(entries) => {
            let mut map = Map::new();
            for (k, v) in entries {
                map.insert(to_cbor(k)?, to_cbor(v)?);
            }
            Ok(CBOR::from(map))
        }
        LogicalValue::Tag(tag, inner) => Ok(CBOR::to_tagged_value(*tag, to_cbor(inner)?)),
        LogicalValue::Bignum { sign, value } => encode_bignum(sign, value),
    }
}

pub fn encode(value: &LogicalValue) -> Result<Vec<u8>, String> {
    Ok(to_cbor(value)?.to_cbor_data())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::logical_value::LogicalValue as LV;

    fn float(value: &str) -> LV {
        LV::Float { width: "auto".to_string(), value: value.to_string() }
    }

    // Expected hex copied verbatim from vectors/v1/cbor/dcbor/hand-written/*.json.

    #[test]
    fn d5_float_whole_becomes_int() {
        assert_eq!(encode(&float("2.0")).unwrap(), hex_decode("02").unwrap());
    }

    #[test]
    fn d5_float_fraction_stays_float() {
        assert_eq!(encode(&float("2.5")).unwrap(), hex_decode("f94100").unwrap());
    }

    #[test]
    fn d6_nan_canonical_payload() {
        assert_eq!(encode(&float("NaN")).unwrap(), hex_decode("f97e00").unwrap());
    }

    #[test]
    fn d7_zero_as_int() {
        assert_eq!(encode(&LV::Int("0".to_string())).unwrap(), hex_decode("00").unwrap());
    }

    #[test]
    fn d7_zero_as_pos_float_unifies_with_int() {
        assert_eq!(encode(&float("0.0")).unwrap(), hex_decode("00").unwrap());
    }

    #[test]
    fn d7_zero_as_neg_float_unifies_with_int() {
        // Opposite of rfc8949's P7: -0.0 must also collapse to plain int 0.
        assert_eq!(encode(&float("-0.0")).unwrap(), hex_decode("00").unwrap());
    }

    #[test]
    fn d8_nfc_combining_accent_normalizes() {
        let v = LV::Text("cafe\u{0301}".to_string());
        assert_eq!(encode(&v).unwrap(), hex_decode("65636166c3a9").unwrap());
    }

    #[test]
    fn bignum_below_native_range_is_rejected() {
        let value = LV::Bignum { sign: "positive".to_string(), value: "5".to_string() };
        assert!(encode(&value).is_err());
    }

    #[test]
    fn bignum_at_2_pow_64_uses_tag_2() {
        let value = LV::Bignum { sign: "positive".to_string(), value: "18446744073709551616".to_string() };
        assert_eq!(encode(&value).unwrap(), hex_decode("c249010000000000000000").unwrap());
    }

    #[test]
    fn tag_wraps_inner_value() {
        let value = LV::Tag(100, Box::new(LV::Int("5".to_string())));
        assert_eq!(encode(&value).unwrap(), vec![0xd8, 0x64, 0x05]);
    }

    #[test]
    fn reserved_bignum_tag_via_generic_tag_arm_is_a_raw_passthrough() {
        // Pins behavior: routing tag 2 (normally the bignum tag) through the
        // generic Tag arm does NOT trigger any dcbor-crate bignum semantics —
        // it is a plain head+inner passthrough, same as rfc8949's Tag arm.
        // If a future dcbor crate upgrade starts special-casing tag 2/3 inside
        // to_tagged_value/to_cbor_data, this test will catch the divergence.
        let value = LV::Tag(2, Box::new(LV::Bytes("01".to_string())));
        assert_eq!(encode(&value).unwrap(), vec![0xc2, 0x41, 0x01]);
    }

    #[test]
    fn explicit_width_is_ignored_whole_float_still_reduces() {
        // D5 must hold even when the input requests a specific width — dCBOR
        // has no "explicit width forcing" concept, unlike rfc8949.
        let v = LV::Float { width: "f64".to_string(), value: "2.0".to_string() };
        assert_eq!(encode(&v).unwrap(), hex_decode("02").unwrap());
    }

    #[test]
    fn explicit_width_is_ignored_nan_still_canonical() {
        // D6 must hold even when the input requests a specific width.
        let v = LV::Float { width: "f32".to_string(), value: "NaN".to_string() };
        assert_eq!(encode(&v).unwrap(), hex_decode("f97e00").unwrap());
    }

    #[test]
    fn array_and_map_round_trip() {
        let arr = LV::Array(vec![LV::Int("1".to_string()), LV::Int("2".to_string())]);
        assert_eq!(encode(&arr).unwrap(), vec![0x82, 0x01, 0x02]);

        let map = LV::Map(vec![(LV::Text("a".to_string()), LV::Int("1".to_string()))]);
        assert_eq!(encode(&map).unwrap(), hex_decode("a1616101").unwrap());
    }
}
