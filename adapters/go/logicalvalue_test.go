package main

import (
	"reflect"
	"testing"
)

func mustParse(t *testing.T, jsonLine string) LogicalValue {
	t.Helper()
	v, err := parseLogicalValueLine(jsonLine)
	if err != nil {
		t.Fatalf("parseLogicalValueLine(%q) error: %v", jsonLine, err)
	}
	return v
}

func TestParsesInt(t *testing.T) {
	v := mustParse(t, `{"type":"int","value":"42"}`)
	if !reflect.DeepEqual(v, IntValue{Value: "42"}) {
		t.Fatalf("got %#v", v)
	}
}

func TestParsesFloat(t *testing.T) {
	v := mustParse(t, `{"type":"float","width":"auto","value":"2.5"}`)
	if !reflect.DeepEqual(v, FloatValue{Width: "auto", Value: "2.5"}) {
		t.Fatalf("got %#v", v)
	}
}

func TestParsesText(t *testing.T) {
	v := mustParse(t, `{"type":"text","value":"café"}`)
	if !reflect.DeepEqual(v, TextValue{Value: "café"}) {
		t.Fatalf("got %#v", v)
	}
}

func TestParsesBytes(t *testing.T) {
	v := mustParse(t, `{"type":"bytes","value":"deadbeef"}`)
	if !reflect.DeepEqual(v, BytesValue{Value: "deadbeef"}) {
		t.Fatalf("got %#v", v)
	}
}

func TestParsesBoolAndNull(t *testing.T) {
	v := mustParse(t, `{"type":"bool","value":true}`)
	if !reflect.DeepEqual(v, BoolValue{Value: true}) {
		t.Fatalf("got %#v", v)
	}
	v2 := mustParse(t, `{"type":"null"}`)
	if !reflect.DeepEqual(v2, NullValue{}) {
		t.Fatalf("got %#v", v2)
	}
}

func TestParsesArray(t *testing.T) {
	v := mustParse(t, `{"type":"array","items":[{"type":"int","value":"1"},{"type":"int","value":"2"}]}`)
	want := ArrayValue{Items: []LogicalValue{IntValue{Value: "1"}, IntValue{Value: "2"}}}
	if !reflect.DeepEqual(v, want) {
		t.Fatalf("got %#v", v)
	}
}

func TestParsesMap(t *testing.T) {
	v := mustParse(t, `{"type":"map","entries":[[{"type":"text","value":"a"},{"type":"int","value":"1"}]]}`)
	want := MapValue{Entries: []MapEntry{{Key: TextValue{Value: "a"}, Val: IntValue{Value: "1"}}}}
	if !reflect.DeepEqual(v, want) {
		t.Fatalf("got %#v", v)
	}
}

func TestParsesTag(t *testing.T) {
	v := mustParse(t, `{"type":"tag","tag":100,"value":{"type":"int","value":"5"}}`)
	want := TagValue{Tag: 100, Value: IntValue{Value: "5"}}
	if !reflect.DeepEqual(v, want) {
		t.Fatalf("got %#v", v)
	}
}

func TestParsesTagAtUint64Max(t *testing.T) {
	// Regression: a naive parse through a signed 64-bit intermediate would
	// silently truncate this. 18446744073709551615 == math.MaxUint64.
	v := mustParse(t, `{"type":"tag","tag":18446744073709551615,"value":{"type":"int","value":"5"}}`)
	want := TagValue{Tag: 18446744073709551615, Value: IntValue{Value: "5"}}
	if !reflect.DeepEqual(v, want) {
		t.Fatalf("got %#v", v)
	}
}

func TestRejectsTagAboveUint64Max(t *testing.T) {
	_, err := parseLogicalValueLine(`{"type":"tag","tag":18446744073709551616,"value":{"type":"int","value":"5"}}`)
	if err == nil {
		t.Fatal("expected error, got nil")
	}
}

func TestParsesBignum(t *testing.T) {
	v := mustParse(t, `{"type":"bignum","sign":"positive","value":"18446744073709551616"}`)
	want := BignumValue{Sign: "positive", Value: "18446744073709551616"}
	if !reflect.DeepEqual(v, want) {
		t.Fatalf("got %#v", v)
	}
}

func TestRejectsUnknownType(t *testing.T) {
	if _, err := parseLogicalValueLine(`{"type":"nonsense"}`); err == nil {
		t.Fatal("expected error, got nil")
	}
}

func TestRejectsMissingType(t *testing.T) {
	if _, err := parseLogicalValueLine(`{}`); err == nil {
		t.Fatal("expected error, got nil")
	}
}
