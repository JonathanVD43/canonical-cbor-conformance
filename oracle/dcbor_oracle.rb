#!/usr/bin/env ruby
# oracle/dcbor_oracle.rb
# Reads one JSON logical-value on stdin, writes hex-encoded canonical dCBOR
# bytes on stdout. Thin wrapper around the independent `cbor-dcbor` gem (see
# Gemfile.lock for the pinned version) -- shares no code with any of this
# project's Rust/Kotlin/TypeScript adapters.
#
# Implementer note: the brief referenced a gem literally named `dcbor` with
# `DCBOR.encode` / `DCBOR::Tagged` entry points. No such gem exists on
# rubygems.org; the real published gem implementing this profile is
# `cbor-dcbor`, whose public API is `Object#to_dcbor` (added via monkeypatch
# to Object/Float/Array/Hash) and `CBOR::Tagged.new(tag, value)` (from its
# `cbor` dependency). Confirmed by reading the gem's installed source
# (`cbor-dcbor.rb`) and bundled test (`test/test-dcbor.rb`). All of this
# task's pinned hex-byte expectations were verified against this real API.
require "json"
require "cbor-dcbor"

def to_ruby(value)
  case value["type"]
  when "int"
    Integer(value["value"])
  when "float"
    lit = value["value"]
    case lit
    when "NaN" then Float::NAN
    when "Infinity" then Float::INFINITY
    when "-Infinity" then -Float::INFINITY
    else Float(lit)
    end
  when "text"
    # dCBOR requires text strings to be normalized to NFC; the cbor-dcbor
    # gem does not do this itself, so it's done here before encoding.
    value["value"].unicode_normalize(:nfc)
  when "bytes"
    [value["value"]].pack("H*")
  when "bool"
    value["value"]
  when "null"
    nil
  when "array"
    value["items"].map { |item| to_ruby(item) }
  when "map"
    value["entries"].each_with_object({}) { |(k, v), h| h[to_ruby(k)] = to_ruby(v) }
  when "tag"
    CBOR::Tagged.new(value["tag"], to_ruby(value["value"]))
  when "bignum"
    magnitude = Integer(value["value"])
    value["sign"] == "positive" ? magnitude : -magnitude
  else
    raise "unknown logical-value type: #{value['type']}"
  end
end

input = JSON.parse(STDIN.read)
ruby_value = to_ruby(input)
encoded = ruby_value.to_dcbor
puts encoded.unpack1("H*")
