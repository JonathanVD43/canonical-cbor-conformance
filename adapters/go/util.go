package main

import "encoding/hex"

// Thin wrappers over stdlib encoding/hex: it already lowercases on encode
// and rejects odd-length/non-hex input on decode, so no hand-rolled hex
// codec is needed here (unlike the Kotlin/TS ports, which had no equivalent
// stdlib in their runtime).

func hexEncode(b []byte) string {
	return hex.EncodeToString(b)
}

func hexDecodeStrict(s string) ([]byte, error) {
	b, err := hex.DecodeString(s)
	if err != nil {
		return nil, encodeErrf("invalid hex string %q: %v", s, err)
	}
	return b, nil
}
