package main

import (
	"encoding/hex"
	"testing"
)

func mustHex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("bad hex %q: %v", s, err)
	}
	return b
}

func mustEncodeRfc8949(t *testing.T, v LogicalValue) []byte {
	t.Helper()
	b, err := encodeRfc8949(v)
	if err != nil {
		t.Fatalf("encodeRfc8949(%#v) error: %v", v, err)
	}
	return b
}

func assertHexEqual(t *testing.T, got []byte, wantHex string) {
	t.Helper()
	if hex.EncodeToString(got) != wantHex {
		t.Fatalf("got %x, want %s", got, wantHex)
	}
}

func TestRfc8949ShortestIntZero(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "0"}), "00")
}

func TestRfc8949ShortestIntBoundary23_24(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "23"}), "17")
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "24"}), "1818")
}

func TestRfc8949ShortestIntBoundary255_256(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "255"}), "18ff")
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "256"}), "190100")
}

func TestRfc8949ShortestIntBoundary65535_65536(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "65535"}), "19ffff")
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "65536"}), "1a00010000")
}

func TestRfc8949ShortestIntBoundary4294967295_4294967296(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "4294967295"}), "1affffffff")
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "4294967296"}), "1b0000000100000000")
}

func TestRfc8949NegativeInt(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "-1"}), "20")
}

func TestRfc8949NegativeIntMinI64Boundary(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, IntValue{Value: "-9223372036854775808"}), "3b7fffffffffffffff")
}

func TestRfc8949TextAndBytes(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, TextValue{Value: "a"}), "6161")
	assertHexEqual(t, mustEncodeRfc8949(t, BytesValue{Value: "deadbeef"}), "44deadbeef")
}

func TestRfc8949BoolAndNull(t *testing.T) {
	assertHexEqual(t, mustEncodeRfc8949(t, BoolValue{Value: true}), "f5")
	assertHexEqual(t, mustEncodeRfc8949(t, BoolValue{Value: false}), "f4")
	assertHexEqual(t, mustEncodeRfc8949(t, NullValue{}), "f6")
}

func TestRfc8949ArrayOfInts(t *testing.T) {
	arr := ArrayValue{Items: []LogicalValue{IntValue{Value: "1"}, IntValue{Value: "2"}}}
	assertHexEqual(t, mustEncodeRfc8949(t, arr), "820102")
}

func TestRfc8949NanCanonicalPayload(t *testing.T) {
	v := FloatValue{Width: "auto", Value: "NaN"}
	assertHexEqual(t, mustEncodeRfc8949(t, v), "f97e00")
}

func TestRfc8949NanCanonicalPayloadUnderExplicitWidthForcing(t *testing.T) {
	for _, width := range []string{"f16", "f32", "f64"} {
		v := FloatValue{Width: width, Value: "NaN"}
		assertHexEqual(t, mustEncodeRfc8949(t, v), "f97e00")
	}
}

func TestRfc8949NegativeZeroPreservedDistinctFromZero(t *testing.T) {
	neg := FloatValue{Width: "auto", Value: "-0.0"}
	pos := FloatValue{Width: "auto", Value: "0.0"}
	negBytes := mustEncodeRfc8949(t, neg)
	posBytes := mustEncodeRfc8949(t, pos)
	assertHexEqual(t, negBytes, "f98000")
	assertHexEqual(t, posBytes, "f90000")
	if hex.EncodeToString(negBytes) == hex.EncodeToString(posBytes) {
		t.Fatal("expected -0.0 and 0.0 to encode differently")
	}
}

func TestRfc8949ShortestFloatFormF16Exact(t *testing.T) {
	v := FloatValue{Width: "auto", Value: "2.5"}
	assertHexEqual(t, mustEncodeRfc8949(t, v), "f94100")
}

func TestRfc8949ExplicitFloatWidthForcesEncoding(t *testing.T) {
	v := FloatValue{Width: "f64", Value: "2.5"}
	assertHexEqual(t, mustEncodeRfc8949(t, v), "fb4004000000000000")
}

func TestRfc8949ExplicitFloatWidthRaisesOnPrecisionLoss(t *testing.T) {
	v := FloatValue{Width: "f16", Value: "0.1"}
	if _, err := encodeRfc8949(v); err == nil {
		t.Fatal("expected error")
	}
}

func TestRfc8949MapKeysSortedByEncodedBytes(t *testing.T) {
	value := MapValue{Entries: []MapEntry{
		{Key: IntValue{Value: "9"}, Val: IntValue{Value: "1"}},
		{Key: IntValue{Value: "10"}, Val: IntValue{Value: "2"}},
	}}
	assertHexEqual(t, mustEncodeRfc8949(t, value), "a209010a02")
}

func TestRfc8949MapKeysPureBytewiseNoLengthPresort(t *testing.T) {
	value := MapValue{Entries: []MapEntry{
		{Key: IntValue{Value: "-24"}, Val: IntValue{Value: "1"}},
		{Key: IntValue{Value: "1000"}, Val: IntValue{Value: "2"}},
	}}
	assertHexEqual(t, mustEncodeRfc8949(t, value), "a21903e8023701")
}

func TestRfc8949BignumAt2Pow64UsesTag2(t *testing.T) {
	value := BignumValue{Sign: "positive", Value: "18446744073709551616"}
	assertHexEqual(t, mustEncodeRfc8949(t, value), "c249010000000000000000")
}

func TestRfc8949NegativeBignumOffsetByOne(t *testing.T) {
	value := BignumValue{Sign: "negative", Value: "18446744073709551616"}
	assertHexEqual(t, mustEncodeRfc8949(t, value), "c348ffffffffffffffff")
}

func TestRfc8949BignumBelow2Pow64IsRejected(t *testing.T) {
	positive := BignumValue{Sign: "positive", Value: "5"}
	if _, err := encodeRfc8949(positive); err == nil {
		t.Fatal("expected error")
	}
	negative := BignumValue{Sign: "negative", Value: "9223372036854775808"}
	if _, err := encodeRfc8949(negative); err == nil {
		t.Fatal("expected error")
	}
}

func TestRfc8949TagWrapsInnerValue(t *testing.T) {
	value := TagValue{Tag: 100, Value: IntValue{Value: "5"}}
	assertHexEqual(t, mustEncodeRfc8949(t, value), "d86405")
}

func TestRfc8949TagAtUint64MaxDoesNotTruncate(t *testing.T) {
	value := TagValue{Tag: 18446744073709551615, Value: IntValue{Value: "5"}}
	assertHexEqual(t, mustEncodeRfc8949(t, value), "dbffffffffffffffff05")
}

func TestRfc8949MapDuplicateKeysAreNotDeduped(t *testing.T) {
	// rfc8949 (P4/P9) has no key-collision dedup logic, unlike dcbor -- both
	// entries for a literal duplicate key must survive to the output.
	value := MapValue{Entries: []MapEntry{
		{Key: IntValue{Value: "5"}, Val: IntValue{Value: "1"}},
		{Key: IntValue{Value: "5"}, Val: IntValue{Value: "2"}},
	}}
	assertHexEqual(t, mustEncodeRfc8949(t, value), "a205010502")
}
