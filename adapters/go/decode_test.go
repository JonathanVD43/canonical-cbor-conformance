package main

import "testing"

func accept(t *testing.T, input []byte, profile Profile) []byte {
	t.Helper()
	v, err := decodeStrict(input, profile)
	if err != nil {
		t.Fatalf("decodeStrict error: %v", err)
	}
	if !v.Accept {
		t.Fatalf("expected Accept, got Reject(%s)", v.Reason)
	}
	return v.Bytes
}

func reject(t *testing.T, input []byte, profile Profile) string {
	t.Helper()
	v, err := decodeStrict(input, profile)
	if err != nil {
		t.Fatalf("decodeStrict error: %v", err)
	}
	if v.Accept {
		t.Fatalf("expected Reject, got Accept(%x)", v.Bytes)
	}
	return v.Reason
}

func TestDecodeAcceptsCanonicalIntAndRoundTrips(t *testing.T) {
	b := mustHex(t, "05")
	assertHexEqual(t, accept(t, b, ProfileRFC8949), "05")
	assertHexEqual(t, accept(t, b, ProfileDCBOR), "05")
}

func TestDecodeAcceptsCanonicalMapAndArray(t *testing.T) {
	arr := mustHex(t, "820102")
	assertHexEqual(t, accept(t, arr, ProfileRFC8949), "820102")

	m := mustHex(t, "a10102")
	assertHexEqual(t, accept(t, m, ProfileRFC8949), "a10102")
}

func TestDecodeNonShortestInt(t *testing.T) {
	b := mustHex(t, "1805")
	if got := reject(t, b, ProfileRFC8949); got != "NON_SHORTEST_INT" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeUnsortedMapKeys(t *testing.T) {
	b := mustHex(t, "a20a010902")
	if got := reject(t, b, ProfileRFC8949); got != "UNSORTED_MAP_KEYS" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeIndefiniteLength(t *testing.T) {
	b := mustHex(t, "9fff")
	if got := reject(t, b, ProfileRFC8949); got != "INDEFINITE_LENGTH" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeDuplicateKey(t *testing.T) {
	b := mustHex(t, "a201010102")
	if got := reject(t, b, ProfileRFC8949); got != "DUPLICATE_KEY" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeNonShortestFloat(t *testing.T) {
	b := mustHex(t, "fa40200000")
	if got := reject(t, b, ProfileRFC8949); got != "NON_SHORTEST_FLOAT" {
		t.Fatalf("got %s", got)
	}
	if got := reject(t, b, ProfileDCBOR); got != "NON_SHORTEST_FLOAT" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeNanPayloadVariant(t *testing.T) {
	b := mustHex(t, "fa7fc00000")
	if got := reject(t, b, ProfileRFC8949); got != "NAN_PAYLOAD_VARIANT" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeTrailingBytes(t *testing.T) {
	b := mustHex(t, "05ff")
	if got := reject(t, b, ProfileRFC8949); got != "TRAILING_BYTES" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeMultipleTopLevelItems(t *testing.T) {
	b := mustHex(t, "0506")
	if got := reject(t, b, ProfileRFC8949); got != "MULTIPLE_TOP_LEVEL_ITEMS" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeUnknownTag(t *testing.T) {
	b := mustHex(t, "d86405")
	if got := reject(t, b, ProfileRFC8949); got != "UNKNOWN_TAG" {
		t.Fatalf("got %s", got)
	}
}

func TestDecodeNonNfcStringDcborOnly(t *testing.T) {
	// "cafe" + combining acute accent (U+0301), not normalized to NFC.
	b := mustHex(t, "6663616665cc81")
	if got := reject(t, b, ProfileDCBOR); got != "NON_NFC_STRING" {
		t.Fatalf("got %s", got)
	}
	assertHexEqual(t, accept(t, b, ProfileRFC8949), "6663616665cc81")
}

func TestDecodeUnreducedNumericDcborOnly(t *testing.T) {
	// 2.0 encoded at its shortest float width (f16) -- still must be a plain
	// int under dcbor's D5/D7 rules.
	b := mustHex(t, "f94000")
	if got := reject(t, b, ProfileDCBOR); got != "UNREDUCED_NUMERIC" {
		t.Fatalf("got %s", got)
	}
	assertHexEqual(t, accept(t, b, ProfileRFC8949), "f94000")
}
