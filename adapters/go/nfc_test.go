package main

import "testing"

func TestNfcComposesCombiningAcuteAccent(t *testing.T) {
	// "cafe" + combining acute accent (U+0301) must normalize to the
	// precomposed "café" (U+00E9).
	decomposed := string([]rune{'c', 'a', 'f', 'e', 0x0301})
	precomposed := string([]rune{'c', 'a', 'f', 0x00e9})
	if got := nfcNormalize(decomposed); got != precomposed {
		t.Fatalf("got %q, want %q", got, precomposed)
	}
}

func TestNfcAlreadyNormalizedIsUnchanged(t *testing.T) {
	precomposed := string([]rune{'c', 'a', 'f', 0x00e9})
	if got := nfcNormalize(precomposed); got != precomposed {
		t.Fatalf("got %q", got)
	}
}

func TestNfcAsciiUnchanged(t *testing.T) {
	if got := nfcNormalize("hello world"); got != "hello world" {
		t.Fatalf("got %q", got)
	}
}

func TestNfcHangulComposesAlgorithmically(t *testing.T) {
	// Hangul syllable GA (U+AC00) decomposes to L+V jamo U+1100 U+1161, and
	// must recompose back exactly (handled algorithmically, not via table).
	decomposed := string([]rune{0x1100, 0x1161})
	precomposed := string([]rune{0xac00})
	if got := nfcNormalize(decomposed); got != precomposed {
		t.Fatalf("got %q (%x), want %q", got, []rune(got), precomposed)
	}
}

func TestNfcMultipleCombiningMarksReorderAndCompose(t *testing.T) {
	// A base char 'q' followed by combining dot-below (U+0323, ccc=220) and
	// combining acute accent (U+0301, ccc=230) must normalize identically
	// regardless of which order the two combining marks appear in the input
	// (canonical ordering is combining-class-driven, not input-order-driven).
	orderA := string([]rune{'q', 0x0323, 0x0301})
	orderB := string([]rune{'q', 0x0301, 0x0323})
	a := nfcNormalize(orderA)
	b := nfcNormalize(orderB)
	if a != b {
		t.Fatalf("expected same normalization regardless of input order: %q vs %q", a, b)
	}
}
