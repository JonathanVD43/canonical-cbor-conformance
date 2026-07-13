package main

import (
	"testing"
)

func floatV(value string) FloatValue { return FloatValue{Width: "auto", Value: value} }

func mustEncodeDcbor(t *testing.T, v LogicalValue) []byte {
	t.Helper()
	b, err := encodeDcbor(v)
	if err != nil {
		t.Fatalf("encodeDcbor(%#v) error: %v", v, err)
	}
	return b
}

func TestDcborD5FloatWholeBecomesInt(t *testing.T) {
	assertHexEqual(t, mustEncodeDcbor(t, floatV("2.0")), "02")
}

func TestDcborD5FloatFractionStaysFloat(t *testing.T) {
	assertHexEqual(t, mustEncodeDcbor(t, floatV("2.5")), "f94100")
}

func TestDcborD6NanCanonicalPayload(t *testing.T) {
	assertHexEqual(t, mustEncodeDcbor(t, floatV("NaN")), "f97e00")
}

func TestDcborD7ZeroAsInt(t *testing.T) {
	assertHexEqual(t, mustEncodeDcbor(t, IntValue{Value: "0"}), "00")
}

func TestDcborD7ZeroAsPosFloatUnifiesWithInt(t *testing.T) {
	assertHexEqual(t, mustEncodeDcbor(t, floatV("0.0")), "00")
}

func TestDcborD7ZeroAsNegFloatUnifiesWithInt(t *testing.T) {
	// Opposite of rfc8949's P7: -0.0 must also collapse to plain int 0.
	assertHexEqual(t, mustEncodeDcbor(t, floatV("-0.0")), "00")
}

func TestDcborD8NfcCombiningAccentNormalizes(t *testing.T) {
	v := TextValue{Value: "café"}
	assertHexEqual(t, mustEncodeDcbor(t, v), "65636166c3a9")
}

func TestDcborBignumBelowNativeRangeIsRejected(t *testing.T) {
	value := BignumValue{Sign: "positive", Value: "5"}
	if _, err := encodeDcbor(value); err == nil {
		t.Fatal("expected error")
	}
}

func TestDcborBignumAt2Pow64UsesTag2(t *testing.T) {
	value := BignumValue{Sign: "positive", Value: "18446744073709551616"}
	assertHexEqual(t, mustEncodeDcbor(t, value), "c249010000000000000000")
}

func TestDcborTagWrapsInnerValue(t *testing.T) {
	value := TagValue{Tag: 100, Value: IntValue{Value: "5"}}
	assertHexEqual(t, mustEncodeDcbor(t, value), "d86405")
}

func TestDcborReservedBignumTagViaGenericTagArmIsARawPassthrough(t *testing.T) {
	value := TagValue{Tag: 2, Value: BytesValue{Value: "01"}}
	assertHexEqual(t, mustEncodeDcbor(t, value), "c24101")
}

func TestDcborExplicitWidthIsIgnoredWholeFloatStillReduces(t *testing.T) {
	v := FloatValue{Width: "f64", Value: "2.0"}
	assertHexEqual(t, mustEncodeDcbor(t, v), "02")
}

func TestDcborExplicitWidthIsIgnoredNanStillCanonical(t *testing.T) {
	v := FloatValue{Width: "f32", Value: "NaN"}
	assertHexEqual(t, mustEncodeDcbor(t, v), "f97e00")
}

func TestDcborTagAtUint64MaxDoesNotTruncate(t *testing.T) {
	value := TagValue{Tag: 18446744073709551615, Value: IntValue{Value: "5"}}
	assertHexEqual(t, mustEncodeDcbor(t, value), "dbffffffffffffffff05")
}

func TestDcborMapKeyCollisionDedupesLastWriteWins(t *testing.T) {
	// D7: 0, 0.0, and -0.0 all canonicalize to encoded key 0x00, so these
	// three logical entries collapse to one, keeping only the last value
	// written (3).
	value := MapValue{Entries: []MapEntry{
		{Key: IntValue{Value: "0"}, Val: IntValue{Value: "1"}},
		{Key: floatV("0.0"), Val: IntValue{Value: "2"}},
		{Key: floatV("-0.0"), Val: IntValue{Value: "3"}},
	}}
	assertHexEqual(t, mustEncodeDcbor(t, value), "a10003")
}

func TestDcborMapKeyCollisionViaNfcDedupesLastWriteWins(t *testing.T) {
	// D8: "café" (precomposed) and "café" (NFC-decomposed) both normalize to
	// identical encoded key bytes, so they must also collapse, last write wins.
	decomposed := "café" // "e" + combining acute accent
	precomposed := "café" // single precomposed char
	value := MapValue{Entries: []MapEntry{
		{Key: TextValue{Value: decomposed}, Val: IntValue{Value: "1"}},
		{Key: TextValue{Value: precomposed}, Val: IntValue{Value: "2"}},
	}}
	assertHexEqual(t, mustEncodeDcbor(t, value), "a165636166c3a902")
}

func TestDcborArrayAndMapRoundTrip(t *testing.T) {
	arr := ArrayValue{Items: []LogicalValue{IntValue{Value: "1"}, IntValue{Value: "2"}}}
	assertHexEqual(t, mustEncodeDcbor(t, arr), "820102")

	m := MapValue{Entries: []MapEntry{{Key: TextValue{Value: "a"}, Val: IntValue{Value: "1"}}}}
	assertHexEqual(t, mustEncodeDcbor(t, m), "a1616101")
}
