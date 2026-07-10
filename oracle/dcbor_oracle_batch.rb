#!/usr/bin/env ruby
# oracle/dcbor_oracle_batch.rb
# Batch variant of dcbor_oracle.rb for bulk fuzz-corpus validation: reads one
# JSON logical-value per line on stdin, writes one line per input on stdout
# (lowercase hex on success, blank line on any error) so line N of output
# always corresponds to line N of input. Shares to_ruby with dcbor_oracle.rb
# in spirit only (duplicated, not required, to keep this a disposable
# generation-time tool rather than a second production oracle entry point).
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
    two_pow_64 = 1 << 64
    # dcbor_oracle.rb (and the underlying cbor-dcbor gem) has no native-range
    # floor check for bignums, unlike the Rust adapters' shared
    # bignum_tag_and_bytes helper. Enforce the same floor here so this batch
    # tool never hands out an "expected" encoding the reference adapter would
    # legitimately reject.
    raise "bignum magnitude #{magnitude} below native range floor" if magnitude < two_pow_64
    value["sign"] == "positive" ? magnitude : -magnitude
  else
    raise "unknown logical-value type: #{value['type']}"
  end
end

STDIN.each_line do |line|
  line = line.strip
  if line.empty?
    puts ""
    next
  end
  begin
    input = JSON.parse(line)
    ruby_value = to_ruby(input)
    encoded = ruby_value.to_dcbor
    puts encoded.unpack1("H*")
  rescue StandardError
    puts ""
  end
  STDOUT.flush
end
