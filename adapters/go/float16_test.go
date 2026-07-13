package main

import (
	"encoding/hex"
	"math"
	"testing"
)

func bitsFromHex(t *testing.T, s string) uint16 {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil || len(b) != 2 {
		t.Fatalf("bad hex %q", s)
	}
	return uint16(b[0])<<8 | uint16(b[1])
}

func TestF16ZeroAndNegativeZero(t *testing.T) {
	if got := doubleToF16Bits(0.0); got != bitsFromHex(t, "0000") {
		t.Fatalf("got %04x", got)
	}
	if got := doubleToF16Bits(math.Copysign(0, -1)); got != bitsFromHex(t, "8000") {
		t.Fatalf("got %04x", got)
	}
	if f16BitsToDouble(bitsFromHex(t, "0000")) != 0.0 {
		t.Fatal("expected +0.0")
	}
	back := f16BitsToDouble(bitsFromHex(t, "8000"))
	if math.Float64bits(back) != math.Float64bits(math.Copysign(0, -1)) {
		t.Fatalf("expected -0.0 bit pattern, got %v", back)
	}
}

func TestF16CanonicalNanPayload(t *testing.T) {
	if got := doubleToF16Bits(math.NaN()); got != bitsFromHex(t, "7e00") {
		t.Fatalf("got %04x", got)
	}
}

func TestF16Infinity(t *testing.T) {
	if got := doubleToF16Bits(math.Inf(1)); got != bitsFromHex(t, "7c00") {
		t.Fatalf("got %04x", got)
	}
	if got := doubleToF16Bits(math.Inf(-1)); got != bitsFromHex(t, "fc00") {
		t.Fatalf("got %04x", got)
	}
	if f16BitsToDouble(bitsFromHex(t, "7c00")) != math.Inf(1) {
		t.Fatal("expected +Inf")
	}
	if f16BitsToDouble(bitsFromHex(t, "fc00")) != math.Inf(-1) {
		t.Fatal("expected -Inf")
	}
}

func TestF16ExactlyRepresentableValueRoundTrips(t *testing.T) {
	b, ok := tryF16(2.5)
	if !ok {
		t.Fatal("expected ok")
	}
	if hex.EncodeToString(b) != "4100" {
		t.Fatalf("got %x", b)
	}
}

func TestF16NonExactValueRejected(t *testing.T) {
	if _, ok := tryF16(0.1); ok {
		t.Fatal("expected not ok")
	}
}

func TestF16SmallestNormalAndSubnormalBoundary(t *testing.T) {
	smallestNormal := math.Pow(2.0, -14.0)
	if got := doubleToF16Bits(smallestNormal); got != bitsFromHex(t, "0400") {
		t.Fatalf("got %04x", got)
	}
	smallestSubnormal := math.Pow(2.0, -24.0)
	if got := doubleToF16Bits(smallestSubnormal); got != bitsFromHex(t, "0001") {
		t.Fatalf("got %04x", got)
	}
}

func TestF16MaxFiniteHalfRoundTrips(t *testing.T) {
	if got := doubleToF16Bits(65504.0); got != bitsFromHex(t, "7bff") {
		t.Fatalf("got %04x", got)
	}
	if f16BitsToDouble(bitsFromHex(t, "7bff")) != 65504.0 {
		t.Fatal("expected 65504.0")
	}
}

func TestF16OverflowRoundsToInfinity(t *testing.T) {
	if got := doubleToF16Bits(65520.0); got != bitsFromHex(t, "7c00") {
		t.Fatalf("got %04x", got)
	}
}

func TestF16SubnormalExhaustiveRoundTrip(t *testing.T) {
	// Regression test for a found-and-fixed bug: f16BitsToDouble's subnormal
	// branch used to start expAdj at -1 instead of 1, undercounting the
	// normalization shift by 2 and silently quartering the magnitude of
	// every subnormal f16 value on decode. No existing vector exercised
	// this (only the encode direction was covered by
	// TestF16SmallestNormalAndSubnormalBoundary above); caught while
	// porting this logic to the C adapter via an exhaustive round-trip
	// sweep there, then reproduced and fixed here.
	for bits := uint16(0); bits <= 0x03FF; bits++ {
		got := f16BitsToDouble(bits)
		want := float64(bits) * 0x1p-24
		if got != want {
			t.Fatalf("bits=0x%04x got=%v want=%v", bits, got, want)
		}
	}
}

func TestF32RoundTrip(t *testing.T) {
	b, ok := tryF32(2.5)
	if !ok {
		t.Fatal("expected ok")
	}
	if hex.EncodeToString(b) != "40200000" {
		t.Fatalf("got %x", b)
	}
	if _, ok := tryF32(0.1); ok {
		t.Fatal("expected not ok")
	}
}
