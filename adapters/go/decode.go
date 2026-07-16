package main

import (
	"fmt"
	"math"
	"math/big"
	"strconv"
	"unicode/utf8"
)

// Strict canonical-CBOR decoder (SPEC.md's `decode-strict` mode). Parses raw
// CBOR bytes into a byte-range-aware intermediate form and rejects any
// well-formed-but-non-canonical input with the single most specific
// decode-strict reason code (P1-P9 / D1-D9). Hand-rolled (not delegated to a
// generic CBOR library) because a generic decode normalizes away exactly the
// information needed to tell *why* something is non-canonical: which
// additional-info width was used, raw NaN payload bits, raw map-key byte
// order.
//
// Bignum rule (SPEC.md), matching the Rust/Kotlin/TypeScript adapters: a
// tag-2/3 (bignum) item is rejected with NON_CANONICAL_BIGNUM if its
// magnitude fits the native 64-bit range or its byte-string payload is
// non-minimal (a leading zero byte). A genuinely canonical bignum still
// ACCEPTs.

type Profile int

const (
	ProfileRFC8949 Profile = iota
	ProfileDCBOR
)

// DecodeError carries either a known decode-strict reason code, or an
// "internal: ..." diagnostic for malformed/truncated input that isn't
// covered by any reason code.
type DecodeError struct{ ReasonOrMessage string }

func (e *DecodeError) Error() string { return e.ReasonOrMessage }

func decodeErrf(format string, args ...any) error {
	return &DecodeError{ReasonOrMessage: fmt.Sprintf(format, args...)}
}

func decodeReject(reason string) error {
	return &DecodeError{ReasonOrMessage: reason}
}

var reasonCodes = map[string]bool{
	"NON_SHORTEST_INT":         true,
	"UNSORTED_MAP_KEYS":        true,
	"INDEFINITE_LENGTH":        true,
	"DUPLICATE_KEY":            true,
	"NON_SHORTEST_FLOAT":       true,
	"NAN_PAYLOAD_VARIANT":      true,
	"TRAILING_BYTES":           true,
	"MULTIPLE_TOP_LEVEL_ITEMS": true,
	"UNKNOWN_TAG":              true,
	"NON_NFC_STRING":           true,
	"UNREDUCED_NUMERIC":        true,
	"NON_CANONICAL_BIGNUM":     true,
}

// allowedTags is the UNKNOWN_TAG allow-list: only the bignum tags are
// exercised by the current vector corpus.
var allowedTags = map[uint64]bool{2: true, 3: true}

// Verdict is the outcome of a single decode-strict input line.
type Verdict struct {
	Accept bool
	Bytes  []byte // valid iff Accept
	Reason string // valid iff !Accept
}

type cursor struct{ pos int }

// decodeStrict parses input and returns a Verdict. A non-nil error means an
// internal (non-reason-code) failure; callers should treat that the same
// way the reference adapters do -- print a diagnostic and an empty output
// line, not crash the batch loop.
func decodeStrict(input []byte, profile Profile) (Verdict, error) {
	if len(input) == 0 {
		return Verdict{}, decodeErrf("internal: empty input line")
	}
	c := &cursor{0}
	item, err := parseItem(input, c, profile)
	if err != nil {
		de, ok := err.(*DecodeError)
		if ok && reasonCodes[de.ReasonOrMessage] {
			return Verdict{Accept: false, Reason: de.ReasonOrMessage}, nil
		}
		return Verdict{}, err
	}

	if c.pos < len(input) {
		c2 := &cursor{c.pos}
		_, err := parseItem(input, c2, profile)
		if err == nil && c2.pos == len(input) {
			return Verdict{Accept: false, Reason: "MULTIPLE_TOP_LEVEL_ITEMS"}, nil
		}
		return Verdict{Accept: false, Reason: "TRAILING_BYTES"}, nil
	}

	logical := itemToLogical(item)
	var reencoded []byte
	var encErr error
	if profile == ProfileRFC8949 {
		reencoded, encErr = encodeRfc8949(logical)
	} else {
		reencoded, encErr = encodeDcbor(logical)
	}
	if encErr != nil {
		return Verdict{}, decodeErrf("internal: canonical input failed to re-encode: %v", encErr)
	}
	return Verdict{Accept: true, Bytes: reencoded}, nil
}

// item is the sealed intermediate parse-tree type (mirrors the Kotlin
// adapter's Item sealed class).
type item interface{ isItem() }

type itemIntPos struct{ v uint64 }
type itemIntNeg struct{ v uint64 }
type itemBytes struct{ v []byte }
type itemText struct{ v string }
type itemArr struct{ items []item }
type itemMapEntry struct{ k, v item }
type itemMap struct{ entries []itemMapEntry }
type itemTagged struct {
	tag   uint64
	inner item
}
type itemBool struct{ v bool }
type itemNull struct{}
type itemFloat struct{ v float64 }

func (itemIntPos) isItem() {}
func (itemIntNeg) isItem() {}
func (itemBytes) isItem()  {}
func (itemText) isItem()   {}
func (itemArr) isItem()    {}
func (itemMap) isItem()    {}
func (itemTagged) isItem() {}
func (itemBool) isItem()   {}
func (itemNull) isItem()   {}
func (itemFloat) isItem()  {}

type argWidth int

const (
	widthDirect argWidth = iota
	widthOne
	widthTwo
	widthFour
	widthEight
)

type head struct {
	major      int
	arg        uint64
	width      argWidth
	indefinite bool
}

func readHead(input []byte, c *cursor) (head, error) {
	if c.pos >= len(input) {
		return head{}, decodeErrf("internal: truncated input (expected an item header)")
	}
	b0 := input[c.pos]
	c.pos++
	major := int(b0>>5) & 0x7
	info := b0 & 0x1f
	if info == 31 {
		return head{major: major, indefinite: true}, nil
	}

	switch {
	case info <= 23:
		return head{major: major, arg: uint64(info), width: widthDirect}, nil
	case info == 24:
		if c.pos >= len(input) {
			return head{}, decodeErrf("internal: truncated 1-byte argument")
		}
		v := uint64(input[c.pos])
		c.pos++
		return head{major: major, arg: v, width: widthOne}, nil
	case info == 25:
		if c.pos+2 > len(input) {
			return head{}, decodeErrf("internal: truncated 2-byte argument")
		}
		var v uint64
		for i := 0; i < 2; i++ {
			v = v<<8 | uint64(input[c.pos+i])
		}
		c.pos += 2
		return head{major: major, arg: v, width: widthTwo}, nil
	case info == 26:
		if c.pos+4 > len(input) {
			return head{}, decodeErrf("internal: truncated 4-byte argument")
		}
		var v uint64
		for i := 0; i < 4; i++ {
			v = v<<8 | uint64(input[c.pos+i])
		}
		c.pos += 4
		return head{major: major, arg: v, width: widthFour}, nil
	case info == 27:
		if c.pos+8 > len(input) {
			return head{}, decodeErrf("internal: truncated 8-byte argument")
		}
		var v uint64
		for i := 0; i < 8; i++ {
			v = v<<8 | uint64(input[c.pos+i])
		}
		c.pos += 8
		return head{major: major, arg: v, width: widthEight}, nil
	default:
		return head{}, decodeErrf("internal: reserved additional-info value (28-30)")
	}
}

func shortestWidthFor(arg uint64) argWidth {
	switch {
	case arg < 24:
		return widthDirect
	case arg <= 0xff:
		return widthOne
	case arg <= 0xffff:
		return widthTwo
	case arg <= 0xffffffff:
		return widthFour
	default:
		return widthEight
	}
}

// checkShortestArg enforces P1/D2: every integer *argument* (ints
// themselves, and string/array/map lengths, and tag numbers) must use the
// shortest encoding for its value.
func checkShortestArg(h head) error {
	if h.width != shortestWidthFor(h.arg) {
		return decodeReject("NON_SHORTEST_INT")
	}
	return nil
}

func strictUTF8Decode(b []byte) (string, error) {
	if !utf8.Valid(b) {
		return "", decodeErrf("internal: invalid utf-8 in text string")
	}
	return string(b), nil
}

func parseItem(input []byte, c *cursor, profile Profile) (item, error) {
	h, err := readHead(input, c)
	if err != nil {
		return nil, err
	}

	if h.major == 7 {
		if h.indefinite {
			return nil, decodeErrf("internal: unexpected break byte outside indefinite-length container")
		}
		return parseMajor7(h.width, h.arg, profile)
	}

	if h.indefinite {
		// P3/D1: indefinite-length arrays/maps/byte/text strings are banned
		// outright, regardless of what they'd otherwise contain.
		return nil, decodeReject("INDEFINITE_LENGTH")
	}
	if err := checkShortestArg(h); err != nil {
		return nil, err
	}

	switch h.major {
	case 0:
		return itemIntPos{v: h.arg}, nil
	case 1:
		return itemIntNeg{v: h.arg}, nil
	case 2:
		length := int(h.arg)
		if c.pos+length > len(input) {
			return nil, decodeErrf("internal: truncated byte string")
		}
		b := input[c.pos : c.pos+length]
		c.pos += length
		return itemBytes{v: b}, nil
	case 3:
		length := int(h.arg)
		if c.pos+length > len(input) {
			return nil, decodeErrf("internal: truncated text string")
		}
		b := input[c.pos : c.pos+length]
		s, err := strictUTF8Decode(b)
		if err != nil {
			return nil, err
		}
		c.pos += length
		// D8: dcbor requires NFC-normalized text; rfc8949 has no such rule
		// (P9 is an explicit non-goal).
		if profile == ProfileDCBOR {
			if nfcNormalize(s) != s {
				return nil, decodeReject("NON_NFC_STRING")
			}
		}
		return itemText{v: s}, nil
	case 4:
		length := int(h.arg)
		items := make([]item, length)
		for i := 0; i < length; i++ {
			it, err := parseItem(input, c, profile)
			if err != nil {
				return nil, err
			}
			items[i] = it
		}
		return itemArr{items: items}, nil
	case 5:
		length := int(h.arg)
		entries := make([]itemMapEntry, length)
		type keyRange struct{ start, end int }
		keyRanges := make([]keyRange, length)
		for i := 0; i < length; i++ {
			keyStart := c.pos
			k, err := parseItem(input, c, profile)
			if err != nil {
				return nil, err
			}
			keyEnd := c.pos
			v, err := parseItem(input, c, profile)
			if err != nil {
				return nil, err
			}
			keyRanges[i] = keyRange{keyStart, keyEnd}
			entries[i] = itemMapEntry{k: k, v: v}
		}
		// P2/D3: keys must appear in strictly increasing bytewise order of
		// their own raw encoded bytes. A duplicate key is necessarily
		// adjacent to itself in properly sorted order, so one adjacent pass
		// catches both violations.
		for i := 0; i < len(keyRanges)-1; i++ {
			a := input[keyRanges[i].start:keyRanges[i].end]
			b := input[keyRanges[i+1].start:keyRanges[i+1].end]
			cmp := compareBytesUnsigned(a, b)
			if cmp == 0 {
				return nil, decodeReject("DUPLICATE_KEY")
			}
			if cmp > 0 {
				return nil, decodeReject("UNSORTED_MAP_KEYS")
			}
		}
		return itemMap{entries: entries}, nil
	case 6:
		tag := h.arg
		if !allowedTags[tag] {
			return nil, decodeReject("UNKNOWN_TAG")
		}
		inner, err := parseItem(input, c, profile)
		if err != nil {
			return nil, err
		}
		// Bignum rule: a tag 2/3 payload must be the minimal big-endian
		// encoding of a magnitude >= 2^64. Reject if the magnitude fits the
		// native 64-bit range (a <= 8-byte payload always does, once the
		// leading-zero case is ruled out) or the payload is non-minimal
		// (non-empty with a leading zero byte).
		if tag == 2 || tag == 3 {
			if b, ok := inner.(itemBytes); ok {
				if (len(b.v) >= 1 && b.v[0] == 0) || len(b.v) <= 8 {
					return nil, decodeReject("NON_CANONICAL_BIGNUM")
				}
			}
		}
		return itemTagged{tag: tag, inner: inner}, nil
	default:
		return nil, decodeErrf("internal: major type is 3 bits, always 0-7")
	}
}

type floatWidth int

const (
	floatF16 floatWidth = iota
	floatF32
	floatF64
)

func parseMajor7(width argWidth, arg uint64, profile Profile) (item, error) {
	switch width {
	case widthDirect:
		switch arg {
		case 20:
			return itemBool{v: false}, nil
		case 21:
			return itemBool{v: true}, nil
		case 22:
			return itemNull{}, nil
		default:
			return nil, decodeErrf("internal: unsupported major-7 simple value %d", arg)
		}
	case widthOne:
		return nil, decodeErrf("internal: unsupported major-7 1-byte simple value")
	case widthTwo:
		return checkFloat(arg, floatF16, profile)
	case widthFour:
		return checkFloat(arg, floatF32, profile)
	case widthEight:
		return checkFloat(arg, floatF64, profile)
	default:
		return nil, decodeErrf("internal: unreachable float width")
	}
}

func checkFloat(bits uint64, width floatWidth, profile Profile) (item, error) {
	var value float64
	switch width {
	case floatF16:
		value = f16BitsToDouble(uint16(bits))
	case floatF32:
		value = float64(math.Float32frombits(uint32(bits)))
	case floatF64:
		value = math.Float64frombits(bits)
	}

	if math.IsNaN(value) {
		// P6/D6: canonical NaN is exactly f16 with payload 0x7e00, in both profiles.
		if width != floatF16 || bits != 0x7e00 {
			return nil, decodeReject("NAN_PAYLOAD_VARIANT")
		}
		return itemFloat{v: math.NaN()}, nil
	}

	// D5/D7 (dcbor only): any whole-number float in [-2^63, 2^64-1] --
	// including +/-0.0, per D7 zero unification -- must be a plain int
	// instead. This is checked before, and takes priority over, the general
	// shortest-float-width rule below.
	if profile == ProfileDCBOR && isDcborReducible(value) {
		return nil, decodeReject("UNREDUCED_NUMERIC")
	}

	// P5/D2: the width used must be the narrowest of f16/f32/f64 that
	// round-trips `value` exactly.
	shortest := shortestFloatWidth(value)
	if width != shortest {
		return nil, decodeReject("NON_SHORTEST_FLOAT")
	}

	return itemFloat{v: value}, nil
}

func isDcborReducible(value float64) bool {
	if math.IsInf(value, 0) {
		return false
	}
	if value != math.Floor(value) {
		return false
	}
	return value >= -9223372036854775808.0 && value <= 18446744073709551615.0
}

func shortestFloatWidth(value float64) floatWidth {
	target := math.Float64bits(value)
	f16Bits := doubleToF16Bits(value)
	if math.Float64bits(f16BitsToDouble(f16Bits)) == target {
		return floatF16
	}
	f32 := float32(value)
	if math.Float64bits(float64(f32)) == target {
		return floatF32
	}
	return floatF64
}

func formatFloat(value float64) string {
	if math.IsNaN(value) {
		return "NaN"
	}
	return goFloatLiteral(value)
}

// goFloatLiteral formats a float64 the way strconv.ParseFloat can parse it
// back exactly (shortest round-tripping decimal), including -0.0.
func goFloatLiteral(f float64) string {
	return strconv.FormatFloat(f, 'g', -1, 64)
}

func itemToLogical(it item) LogicalValue {
	switch v := it.(type) {
	case itemIntPos:
		return IntValue{Value: fmt.Sprintf("%d", v.v)}
	case itemIntNeg:
		n := new(big.Int).SetUint64(v.v)
		n = new(big.Int).Sub(big.NewInt(-1), n)
		return IntValue{Value: n.String()}
	case itemBytes:
		return BytesValue{Value: hexEncode(v.v)}
	case itemText:
		return TextValue{Value: v.v}
	case itemArr:
		items := make([]LogicalValue, len(v.items))
		for i, x := range v.items {
			items[i] = itemToLogical(x)
		}
		return ArrayValue{Items: items}
	case itemMap:
		entries := make([]MapEntry, len(v.entries))
		for i, e := range v.entries {
			entries[i] = MapEntry{Key: itemToLogical(e.k), Val: itemToLogical(e.v)}
		}
		return MapValue{Entries: entries}
	case itemTagged:
		return TagValue{Tag: v.tag, Value: itemToLogical(v.inner)}
	case itemBool:
		return BoolValue{Value: v.v}
	case itemNull:
		return NullValue{}
	case itemFloat:
		return FloatValue{Width: "auto", Value: formatFloat(v.v)}
	default:
		panic(fmt.Sprintf("unhandled item type %T", it))
	}
}
