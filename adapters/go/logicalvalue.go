package main

import (
	"encoding/json"
	"fmt"
	"math/big"
	"strings"
)

// LogicalValue mirrors the Kotlin adapter's sealed LogicalValue class: a
// closed set of concrete types implementing a marker method, parsed from
// this project's Neutral Logical Value Grammar (logical-value.schema.json).
type LogicalValue interface {
	isLogicalValue()
}

type IntValue struct{ Value string }
type FloatValue struct{ Width, Value string }
type TextValue struct{ Value string }
type BytesValue struct{ Value string }
type BoolValue struct{ Value bool }
type NullValue struct{}
type ArrayValue struct{ Items []LogicalValue }
type MapEntry struct{ Key, Val LogicalValue }
type MapValue struct{ Entries []MapEntry }
type TagValue struct {
	Tag   uint64
	Value LogicalValue
}
type BignumValue struct{ Sign, Value string }

func (IntValue) isLogicalValue()    {}
func (FloatValue) isLogicalValue()  {}
func (TextValue) isLogicalValue()   {}
func (BytesValue) isLogicalValue()  {}
func (BoolValue) isLogicalValue()   {}
func (NullValue) isLogicalValue()   {}
func (ArrayValue) isLogicalValue()  {}
func (MapValue) isLogicalValue()    {}
func (TagValue) isLogicalValue()    {}
func (BignumValue) isLogicalValue() {}

// ParseError is raised for any malformed logical-value JSON line.
type ParseError struct{ msg string }

func (e *ParseError) Error() string { return e.msg }

func parseErrf(format string, args ...any) error {
	return &ParseError{msg: fmt.Sprintf(format, args...)}
}

var maxUint64Big = new(big.Int).SetUint64(^uint64(0))

// parseLogicalValueLine decodes a single JSON stdin line into a LogicalValue.
// json.Number is used throughout so the "tag" field (a JSON number that can
// exceed a signed 64-bit type's range, up to 2^64-1) is never narrowed
// through a lossy intermediate.
func parseLogicalValueLine(line string) (LogicalValue, error) {
	dec := json.NewDecoder(strings.NewReader(line))
	dec.UseNumber()
	var raw any
	if err := dec.Decode(&raw); err != nil {
		return nil, parseErrf("malformed JSON: %v", err)
	}
	return parseLogicalValue(raw)
}

func parseLogicalValue(raw any) (LogicalValue, error) {
	obj, ok := raw.(map[string]any)
	if !ok {
		return nil, parseErrf("expected a JSON object")
	}
	t, ok := obj["type"].(string)
	if !ok {
		return nil, parseErrf("missing \"type\" field")
	}
	switch t {
	case "int":
		v, err := requiredString(obj, "value", "int")
		if err != nil {
			return nil, err
		}
		return IntValue{Value: v}, nil
	case "float":
		w, err := requiredString(obj, "width", "float")
		if err != nil {
			return nil, err
		}
		v, err := requiredString(obj, "value", "float")
		if err != nil {
			return nil, err
		}
		return FloatValue{Width: w, Value: v}, nil
	case "text":
		v, err := requiredString(obj, "value", "text")
		if err != nil {
			return nil, err
		}
		return TextValue{Value: v}, nil
	case "bytes":
		v, err := requiredString(obj, "value", "bytes")
		if err != nil {
			return nil, err
		}
		return BytesValue{Value: v}, nil
	case "bool":
		v, ok := obj["value"].(bool)
		if !ok {
			return nil, parseErrf("bool: missing \"value\"")
		}
		return BoolValue{Value: v}, nil
	case "null":
		return NullValue{}, nil
	case "array":
		items, ok := obj["items"].([]any)
		if !ok {
			return nil, parseErrf("array: missing \"items\"")
		}
		out := make([]LogicalValue, len(items))
		for i, it := range items {
			v, err := parseLogicalValue(it)
			if err != nil {
				return nil, err
			}
			out[i] = v
		}
		return ArrayValue{Items: out}, nil
	case "map":
		entries, ok := obj["entries"].([]any)
		if !ok {
			return nil, parseErrf("map: missing \"entries\"")
		}
		out := make([]MapEntry, len(entries))
		for i, e := range entries {
			pair, ok := e.([]any)
			if !ok || len(pair) != 2 {
				return nil, parseErrf("map entry must be a 2-element array")
			}
			k, err := parseLogicalValue(pair[0])
			if err != nil {
				return nil, err
			}
			v, err := parseLogicalValue(pair[1])
			if err != nil {
				return nil, err
			}
			out[i] = MapEntry{Key: k, Val: v}
		}
		return MapValue{Entries: out}, nil
	case "tag":
		rawTag, ok := obj["tag"]
		if !ok || rawTag == nil {
			return nil, parseErrf("tag: missing \"tag\" number")
		}
		num, ok := rawTag.(json.Number)
		if !ok {
			return nil, parseErrf("tag: missing \"tag\" number")
		}
		big, ok := new(big.Int).SetString(num.String(), 10)
		if !ok || big.Sign() < 0 || big.Cmp(maxUint64Big) > 0 {
			return nil, parseErrf("tag: missing \"tag\" number")
		}
		rawValue, ok := obj["value"]
		if !ok || rawValue == nil {
			return nil, parseErrf("tag: missing \"value\"")
		}
		inner, err := parseLogicalValue(rawValue)
		if err != nil {
			return nil, err
		}
		return TagValue{Tag: big.Uint64(), Value: inner}, nil
	case "bignum":
		sign, err := requiredString(obj, "sign", "bignum")
		if err != nil {
			return nil, err
		}
		v, err := requiredString(obj, "value", "bignum")
		if err != nil {
			return nil, err
		}
		return BignumValue{Sign: sign, Value: v}, nil
	default:
		return nil, parseErrf("unknown logical-value type: %q", t)
	}
}

func requiredString(obj map[string]any, field, typeName string) (string, error) {
	v, ok := obj[field]
	if !ok || v == nil {
		return "", parseErrf("%s: missing %q", typeName, field)
	}
	s, ok := v.(string)
	if !ok {
		return "", parseErrf("%s: missing %q", typeName, field)
	}
	return s, nil
}
