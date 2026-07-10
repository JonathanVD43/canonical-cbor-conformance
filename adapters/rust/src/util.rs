pub fn hex_encode(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn hex_decode(s: &str) -> Result<Vec<u8>, String> {
    let bytes = s.as_bytes();
    if bytes.len() % 2 != 0 {
        return Err(format!("hex string has odd length: {s:?}"));
    }
    if !bytes.is_ascii() {
        return Err(format!("hex string contains non-ASCII characters: {s:?}"));
    }
    (0..bytes.len())
        .step_by(2)
        .map(|i| {
            let byte_str = std::str::from_utf8(&bytes[i..i + 2]).unwrap();
            u8::from_str_radix(byte_str, 16).map_err(|e| format!("invalid hex byte {byte_str:?}: {e}"))
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trips() {
        assert_eq!(hex_encode(&[0xde, 0xad, 0xbe, 0xef]), "deadbeef");
        assert_eq!(hex_decode("deadbeef").unwrap(), vec![0xde, 0xad, 0xbe, 0xef]);
    }

    #[test]
    fn rejects_odd_length() {
        assert!(hex_decode("abc").is_err());
    }

    #[test]
    fn rejects_non_ascii_without_panicking() {
        assert!(hex_decode("café").is_err());
    }
}
