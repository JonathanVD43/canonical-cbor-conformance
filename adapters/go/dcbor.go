package main

import (
	"math"
	"math/big"
	"sort"
)

// dCBOR profile: D5 (numeric reduction), D6 (NaN pin), D7 (zero
// unification), and D8 (NFC normalization) are implemented directly here.
// D3 (map key sort) and bignum handling reuse the exact same rules as
// rfc8949 (see rfc8949.go).

func encodeFloatDcbor(literal string) ([]byte, error) {
	f, err := parseFloatLiteral(literal)
	if err != nil {
		return nil, err
	}
	if math.IsNaN(f) {
		// D6: NaN always uses the single canonical f16 payload.
		return []byte{0xf9, 0x7e, 0x00}, nil
	}
	if !math.IsInf(f, 0) && f == math.Floor(f) {
		// D5/D7: a float with no fractional part (including -0.0) reduces to
		// the shortest-int form, unifying with the plain integer encoding.
		whole := bigFromWholeFloat(f)
		if whole.Sign() >= 0 {
			if whole.Cmp(twoPow64) < 0 {
				return encodeHead(0, whole), nil
			}
		} else {
			arg := new(big.Int).Sub(big.NewInt(-1), whole)
			if arg.Cmp(twoPow64) < 0 {
				return encodeHead(1, arg), nil
			}
		}
		// Falls through to shortest-float form below if it doesn't fit the
		// native int range (no dcbor vector exercises this edge).
	}
	if b, ok := tryF16(f); ok {
		return append([]byte{0xf9}, b...), nil
	}
	if b, ok := tryF32(f); ok {
		return append([]byte{0xfa}, b...), nil
	}
	return append([]byte{0xfb}, doubleToBeBytes(f)...), nil
}

func bigFromWholeFloat(f float64) *big.Int {
	bf := new(big.Float).SetFloat64(f)
	i, _ := bf.Int(nil)
	return i
}

func encodeBignumDcbor(sign, value string) ([]byte, error) {
	return encodeBignum(sign, value)
}

type dedupEntry struct {
	keyBytes []byte
	valBytes []byte
}

func encodeDcbor(value LogicalValue) ([]byte, error) {
	switch v := value.(type) {
	case IntValue:
		return encodeInt(v.Value)
	case FloatValue:
		return encodeFloatDcbor(v.Value)
	case TextValue:
		// D8: normalize to NFC before UTF-8 encoding.
		normalized := nfcNormalize(v.Value)
		b := []byte(normalized)
		return append(encodeHead(3, big.NewInt(int64(len(b)))), b...), nil
	case BytesValue:
		b, err := hexDecodeStrict(v.Value)
		if err != nil {
			return nil, err
		}
		return append(encodeHead(2, big.NewInt(int64(len(b)))), b...), nil
	case BoolValue:
		if v.Value {
			return []byte{0xf5}, nil
		}
		return []byte{0xf4}, nil
	case NullValue:
		return []byte{0xf6}, nil
	case ArrayValue:
		out := encodeHead(4, big.NewInt(int64(len(v.Items))))
		for _, item := range v.Items {
			b, err := encodeDcbor(item)
			if err != nil {
				return nil, err
			}
			out = append(out, b...)
		}
		return out, nil
	case MapValue:
		// dCBOR's reference encoder stores entries in a real map keyed by
		// canonical-encoded bytes, so two logical entries that canonicalize
		// to the same key (e.g. via D7 zero unification or D8 NFC
		// normalization) collapse to one, last write wins.
		dedup := make(map[string]dedupEntry)
		order := make([]string, 0, len(v.Entries))
		for _, e := range v.Entries {
			encodedKey, err := encodeDcbor(e.Key)
			if err != nil {
				return nil, err
			}
			encodedVal, err := encodeDcbor(e.Val)
			if err != nil {
				return nil, err
			}
			hk := hexEncode(encodedKey)
			if _, exists := dedup[hk]; !exists {
				order = append(order, hk)
			}
			dedup[hk] = dedupEntry{keyBytes: encodedKey, valBytes: encodedVal}
		}
		entries := make([]dedupEntry, len(order))
		for i, hk := range order {
			entries[i] = dedup[hk]
		}
		sort.SliceStable(entries, func(i, j int) bool {
			return compareBytesUnsigned(entries[i].keyBytes, entries[j].keyBytes) < 0
		})
		out := encodeHead(5, big.NewInt(int64(len(entries))))
		for _, e := range entries {
			out = append(out, e.keyBytes...)
			out = append(out, e.valBytes...)
		}
		return out, nil
	case TagValue:
		out := encodeHead(6, new(big.Int).SetUint64(v.Tag))
		inner, err := encodeDcbor(v.Value)
		if err != nil {
			return nil, err
		}
		return append(out, inner...), nil
	case BignumValue:
		return encodeBignumDcbor(v.Sign, v.Value)
	default:
		return nil, encodeErrf("unhandled logical value type %T", value)
	}
}
