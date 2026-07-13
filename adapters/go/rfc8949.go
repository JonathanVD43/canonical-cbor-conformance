package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"math/big"
	"sort"
	"strconv"
)

type EncodeError struct{ msg string }

func (e *EncodeError) Error() string { return e.msg }

func encodeErrf(format string, args ...any) error {
	return &EncodeError{msg: fmt.Sprintf(format, args...)}
}

var twoPow64 = new(big.Int).Lsh(big.NewInt(1), 64)

// encodeHead writes a CBOR item head (major type + argument) using the
// shortest encoding for arg, per P1/D2.
func encodeHead(majorType int, arg *big.Int) []byte {
	mt := byte(majorType << 5)
	switch {
	case arg.Cmp(big.NewInt(24)) < 0:
		return []byte{mt | byte(arg.Int64())}
	case arg.Cmp(big.NewInt(0xff)) <= 0:
		return append([]byte{mt | 24}, byte(arg.Int64()))
	case arg.Cmp(big.NewInt(0xffff)) <= 0:
		out := make([]byte, 3)
		out[0] = mt | 25
		binary.BigEndian.PutUint16(out[1:], uint16(arg.Int64()))
		return out
	case arg.Cmp(big.NewInt(0xffffffff)) <= 0:
		out := make([]byte, 5)
		out[0] = mt | 26
		binary.BigEndian.PutUint32(out[1:], uint32(arg.Int64()))
		return out
	default:
		out := make([]byte, 9)
		out[0] = mt | 27
		binary.BigEndian.PutUint64(out[1:], arg.Uint64())
		return out
	}
}

// encodeInt is reused by dcbor.go (same shortest-int rule applies to both
// profiles).
func encodeInt(value string) ([]byte, error) {
	n, ok := new(big.Int).SetString(value, 10)
	if !ok {
		return nil, encodeErrf("int: invalid integer literal %q", value)
	}
	if n.Sign() >= 0 {
		if n.Cmp(twoPow64) >= 0 {
			return nil, encodeErrf("int: %s exceeds native range", n.String())
		}
		return encodeHead(0, n), nil
	}
	arg := new(big.Int).Sub(big.NewInt(-1), n)
	if arg.Cmp(twoPow64) >= 0 {
		return nil, encodeErrf("int: %s exceeds native range", n.String())
	}
	return encodeHead(1, arg), nil
}

// doubleToBeBytes is reused by dcbor.go.
func doubleToBeBytes(f float64) []byte {
	out := make([]byte, 8)
	binary.BigEndian.PutUint64(out, math.Float64bits(f))
	return out
}

func parseFloatLiteral(literal string) (float64, error) {
	f, err := strconv.ParseFloat(literal, 64)
	if err != nil {
		return 0, encodeErrf("float: invalid literal %q", literal)
	}
	return f, nil
}

func encodeFloat(width, literal string) ([]byte, error) {
	f, err := parseFloatLiteral(literal)
	if err != nil {
		return nil, err
	}
	if math.IsNaN(f) {
		// P6: NaN always uses the single canonical f16 payload, regardless of width.
		return []byte{0xf9, 0x7e, 0x00}, nil
	}
	if width == "auto" {
		if b, ok := tryF16(f); ok {
			return append([]byte{0xf9}, b...), nil
		}
		if b, ok := tryF32(f); ok {
			return append([]byte{0xfa}, b...), nil
		}
		return append([]byte{0xfb}, doubleToBeBytes(f)...), nil
	}
	switch width {
	case "f16":
		b, ok := tryF16(f)
		if !ok {
			return nil, encodeErrf("float: %v cannot round-trip at requested width f16", f)
		}
		return append([]byte{0xf9}, b...), nil
	case "f32":
		b, ok := tryF32(f)
		if !ok {
			return nil, encodeErrf("float: %v cannot round-trip at requested width f32", f)
		}
		return append([]byte{0xfa}, b...), nil
	case "f64":
		return append([]byte{0xfb}, doubleToBeBytes(f)...), nil
	default:
		return nil, encodeErrf("float: unknown width %q", width)
	}
}

func stripLeadingZeros(b []byte) []byte {
	start := 0
	for start < len(b)-1 && b[start] == 0 {
		start++
	}
	return b[start:]
}

// bignumTagAndBytes is shared with the dcbor profile: bignum tag+magnitude
// rules are identical in both profiles.
func bignumTagAndBytes(sign, value string) (int, []byte, error) {
	magnitude, ok := new(big.Int).SetString(value, 10)
	if !ok {
		return 0, nil, encodeErrf("bignum: invalid magnitude %q", value)
	}
	tag := 2
	if sign != "positive" {
		tag = 3
	}
	if magnitude.Cmp(twoPow64) < 0 {
		return 0, nil, encodeErrf(
			"bignum magnitude %s fits in a native CBOR integer and must not be encoded as tag %d (SPEC.md bignum rule)",
			magnitude.String(), tag,
		)
	}
	var rawInt *big.Int
	switch sign {
	case "positive":
		rawInt = magnitude
	case "negative":
		rawInt = new(big.Int).Sub(magnitude, big.NewInt(1))
	default:
		return 0, nil, encodeErrf("bignum: unknown sign %q", sign)
	}
	return tag, stripLeadingZeros(rawInt.Bytes()), nil
}

func encodeBignum(sign, value string) ([]byte, error) {
	tag, bytesOut, err := bignumTagAndBytes(sign, value)
	if err != nil {
		return nil, err
	}
	out := encodeHead(6, big.NewInt(int64(tag)))
	out = append(out, encodeHead(2, big.NewInt(int64(len(bytesOut))))...)
	out = append(out, bytesOut...)
	return out, nil
}

// compareBytesUnsigned is reused by dcbor.go (same bytewise map-key sort
// rule applies to both profiles). Bytewise-lexicographic, shorter-is-less on
// a shared prefix -- exactly bytes.Compare's semantics.
func compareBytesUnsigned(a, b []byte) int {
	return bytes.Compare(a, b)
}

func encodeRfc8949(value LogicalValue) ([]byte, error) {
	switch v := value.(type) {
	case IntValue:
		return encodeInt(v.Value)
	case TextValue:
		b := []byte(v.Value)
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
			b, err := encodeRfc8949(item)
			if err != nil {
				return nil, err
			}
			out = append(out, b...)
		}
		return out, nil
	case FloatValue:
		return encodeFloat(v.Width, v.Value)
	case MapValue:
		type kv struct{ k, v []byte }
		encoded := make([]kv, len(v.Entries))
		for i, e := range v.Entries {
			k, err := encodeRfc8949(e.Key)
			if err != nil {
				return nil, err
			}
			val, err := encodeRfc8949(e.Val)
			if err != nil {
				return nil, err
			}
			encoded[i] = kv{k, val}
		}
		sort.SliceStable(encoded, func(i, j int) bool {
			return compareBytesUnsigned(encoded[i].k, encoded[j].k) < 0
		})
		out := encodeHead(5, big.NewInt(int64(len(v.Entries))))
		for _, e := range encoded {
			out = append(out, e.k...)
			out = append(out, e.v...)
		}
		return out, nil
	case TagValue:
		out := encodeHead(6, new(big.Int).SetUint64(v.Tag))
		inner, err := encodeRfc8949(v.Value)
		if err != nil {
			return nil, err
		}
		return append(out, inner...), nil
	case BignumValue:
		return encodeBignum(v.Sign, v.Value)
	default:
		return nil, encodeErrf("unhandled logical value type %T", value)
	}
}
