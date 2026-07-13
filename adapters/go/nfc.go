package main

// Hand-rolled Unicode Normalization Form C (NFC), stdlib-only. Go's stdlib
// (unlike Java's java.text.Normalizer, JS's String.prototype.normalize, or
// Rust's unicode-normalization crate used elsewhere in this project) has no
// normalization API, and golang.org/x/text is a separate module outside the
// stdlib distribution -- so this reimplements the standard canonical
// decomposition + canonical ordering + canonical composition algorithm
// (Unicode Standard Annex #15) against data tables generated from Python's
// stdlib unicodedata module (see scripts/gen_nfc_tables.py and the
// generated nfc_tables.go). Hangul syllables are handled algorithmically
// per the standard arithmetic Hangul decomposition/composition rule rather
// than via a table.

const (
	hangulSBase  = 0xAC00
	hangulLBase  = 0x1100
	hangulVBase  = 0x1161
	hangulTBase  = 0x11A7
	hangulLCount = 19
	hangulVCount = 21
	hangulTCount = 28
	hangulNCount = hangulVCount * hangulTCount
	hangulSCount = hangulLCount * hangulNCount
)

func isHangulSyllable(r rune) bool {
	return r >= hangulSBase && r < hangulSBase+hangulSCount
}

func combiningClass(r rune) uint8 {
	return nfcCombiningClass[r]
}

// decomposeHangul returns the two-jamo canonical decomposition of a Hangul
// syllable (LV form) or three-jamo (LVT form).
func decomposeHangul(r rune) []rune {
	sIndex := r - hangulSBase
	l := hangulLBase + sIndex/hangulNCount
	v := hangulVBase + (sIndex%hangulNCount)/hangulTCount
	t := sIndex % hangulTCount
	if t == 0 {
		return []rune{l, v}
	}
	return []rune{l, v, hangulTBase + t}
}

// nfcNormalize returns the NFC form of s.
func nfcNormalize(s string) string {
	decomposed := nfdDecompose(s)
	composed := nfcComposeRunes(decomposed)
	return string(composed)
}

func nfdDecompose(s string) []rune {
	out := make([]rune, 0, len(s))
	for _, r := range s {
		switch {
		case isHangulSyllable(r):
			out = append(out, decomposeHangul(r)...)
		default:
			if d, ok := nfcDecomp[r]; ok {
				out = append(out, []rune(d)...)
			} else {
				out = append(out, r)
			}
		}
	}
	canonicalOrder(out)
	return out
}

// canonicalOrder stable-sorts each maximal run of non-starter (ccc != 0)
// runes by combining class, in place.
func canonicalOrder(runes []rune) {
	i := 0
	for i < len(runes) {
		if combiningClass(runes[i]) == 0 {
			i++
			continue
		}
		j := i
		for j < len(runes) && combiningClass(runes[j]) != 0 {
			j++
		}
		// Stable insertion sort of runes[i:j] by combining class -- these
		// runs are short in practice, and this keeps ties in original
		// (already-decomposition-order) relative order.
		for k := i + 1; k < j; k++ {
			v := runes[k]
			vClass := combiningClass(v)
			m := k
			for m > i && combiningClass(runes[m-1]) > vClass {
				runes[m] = runes[m-1]
				m--
			}
			runes[m] = v
		}
		i = j
	}
}

// composeHangul attempts the standard algorithmic Hangul L+V or LV+T
// composition for the pair (a, b).
func composeHangul(a, b rune) (rune, bool) {
	if a >= hangulLBase && a < hangulLBase+hangulLCount && b >= hangulVBase && b < hangulVBase+hangulVCount {
		lIndex := a - hangulLBase
		vIndex := b - hangulVBase
		return hangulSBase + (lIndex*hangulVCount+vIndex)*hangulTCount, true
	}
	if isHangulSyllable(a) && (a-hangulSBase)%hangulTCount == 0 && b > hangulTBase && b < hangulTBase+hangulTCount {
		return a + (b - hangulTBase), true
	}
	return 0, false
}

// nfcComposeRunes applies the standard canonical composition algorithm
// (UAX #15) to an already-canonically-decomposed-and-ordered rune sequence.
func nfcComposeRunes(decomposed []rune) []rune {
	if len(decomposed) == 0 {
		return decomposed
	}
	out := make([]rune, 0, len(decomposed))
	out = append(out, decomposed[0])
	starterIdx := 0
	lastClass := combiningClass(decomposed[0])
	if lastClass != 0 {
		lastClass = 255 // string starting with a combining mark: never blocks the first composition attempt incorrectly
	} else {
		lastClass = 0
	}

	for _, ch := range decomposed[1:] {
		chClass := combiningClass(ch)
		// Per UAX #15's blocking rule (D115): ch may compose with the
		// current base unless some earlier, not-yet-composed character
		// between them has combining class 0 or >= ch's class.
		composed, ok := rune(0), false
		if lastClass == 0 || lastClass < chClass {
			base := out[starterIdx]
			if c, found := nfcCompose[nfcComposePairKey{base, ch}]; found {
				composed, ok = c, true
			} else if c, found := composeHangul(base, ch); found {
				composed, ok = c, true
			}
		}
		if ok {
			out[starterIdx] = composed
			continue
		}
		out = append(out, ch)
		if chClass == 0 {
			starterIdx = len(out) - 1
			lastClass = 0
		} else {
			lastClass = chClass
		}
	}
	return out
}
