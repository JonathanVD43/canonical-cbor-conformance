package main

import "math"

// Hand-rolled IEEE-754 binary16 (f16) <-> binary64 (f64) bit-level
// conversion. Go's stdlib has no float16 type; ported from the same
// round-to-nearest-even algorithm the Rust adapter's `half` crate and the
// Kotlin/TypeScript adapters' Float16 modules use.

func doubleToF16Bits(value float64) uint16 {
	x := math.Float64bits(value)
	sign := (x >> 63) & 1
	exp := (x >> 52) & 0x7ff
	man := x & 0xfffffffffffff

	if exp == 0x7ff {
		// Infinity or NaN.
		newMan := man >> 42
		var nanBit uint64
		if man != 0 {
			nanBit = 1 << 9
		}
		return uint16((sign<<15 | 0x7c00 | newMan | nanBit) & 0xffff)
	}

	halfSignBits := sign << 15
	halfExp := int64(exp) - 1023 + 15

	if halfExp >= 0x1f {
		return uint16((halfSignBits | 0x7c00) & 0xffff)
	}

	if halfExp <= 0 {
		if 10-halfExp > 21 {
			// Underflows even a subnormal half: signed zero.
			return uint16(halfSignBits & 0xffff)
		}
		manWithHidden := man | 0x10000000000000
		halfMan := manWithHidden >> uint(43-halfExp)
		roundBit := uint64(1) << uint(42-halfExp)
		if manWithHidden&roundBit != 0 && manWithHidden&(3*roundBit-1) != 0 {
			halfMan += 1
		}
		return uint16((halfSignBits | halfMan) & 0xffff)
	}

	halfExpBits := uint64(halfExp) << 10
	halfMan := man >> 42
	roundBit := uint64(1) << 41
	combined := halfSignBits | halfExpBits | halfMan
	roundUp := man&roundBit != 0 && man&(3*roundBit-1) != 0
	if roundUp {
		combined += 1
	}
	return uint16(combined & 0xffff)
}

func f16BitsToDouble(bits uint16) float64 {
	i := uint64(bits)
	sign := (i >> 15) & 1
	exp := (i >> 10) & 0x1f
	man := i & 0x3ff
	sign64 := sign << 63

	switch {
	case exp == 0 && man == 0:
		return math.Float64frombits(sign64)
	case exp == 0:
		// Normalize the 10-bit subnormal significand by left-shifting until its
		// leading set bit reaches position 10 (the implicit-bit position),
		// counting shifts via expAdj. expAdj MUST start at 1, not -1: for the
		// smallest subnormal (man=1, true value 2^-24), exactly 10 shifts are
		// needed to reach manAdj=0x400, and the final double exponent must
		// come out to 1023-24=999. That only holds if expAdj lands on
		// 1-10=-9 (giving 1008+(-9)=999); a -1 starting point yields -11,
		// undercounting by 2 and silently quartering the reconstructed
		// magnitude of every subnormal f16 value.
		expAdj := int64(1)
		manAdj := man
		for manAdj&0x400 == 0 {
			manAdj <<= 1
			expAdj -= 1
		}
		manAdj &= 0x3ff
		exp64 := 1023 - 15 + expAdj
		man64 := manAdj << 42
		return math.Float64frombits(sign64 | uint64(exp64)<<52 | man64)
	case exp == 0x1f:
		if man == 0 {
			return math.Float64frombits(sign64 | 0x7ff0000000000000)
		}
		return math.Float64frombits(sign64 | 0x7ff0000000000000 | (man << 42))
	default:
		exp64 := exp - 15 + 1023
		man64 := man << 42
		return math.Float64frombits(sign64 | exp64<<52 | man64)
	}
}

func sameBits(a, b float64) bool {
	return math.Float64bits(a) == math.Float64bits(b)
}

// Callers must handle NaN before reaching these helpers -- sameBits's raw-bits
// comparison never matches NaN to NaN.
func tryF16(f float64) ([]byte, bool) {
	bits := doubleToF16Bits(f)
	back := f16BitsToDouble(bits)
	if !sameBits(back, f) {
		return nil, false
	}
	return []byte{byte(bits >> 8), byte(bits)}, true
}

func tryF32(f float64) ([]byte, bool) {
	v := float32(f)
	back := float64(v)
	if !sameBits(back, f) {
		return nil, false
	}
	bits := math.Float32bits(v)
	return []byte{byte(bits >> 24), byte(bits >> 16), byte(bits >> 8), byte(bits)}, true
}
