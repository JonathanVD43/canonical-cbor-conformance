package adapter;

import com.fasterxml.jackson.databind.JsonNode;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.List;

/**
 * The Neutral Logical Value Grammar (logical-value.schema.json), modelled as
 * a sealed interface of records rather than Kotlin's sealed-class hierarchy
 * — same shape, different language idiom, exercised with Java 21 pattern
 * matching on the encode/decode side instead of Kotlin's `when`.
 */
public sealed interface LogicalValue {
    record Int(String value) implements LogicalValue {}
    record Flt(String width, String value) implements LogicalValue {}
    record Text(String value) implements LogicalValue {}
    record Bytes(String value) implements LogicalValue {}
    record Bool(boolean value) implements LogicalValue {}
    record Null() implements LogicalValue {}
    record Arr(List<LogicalValue> items) implements LogicalValue {}
    record Map(List<java.util.Map.Entry<LogicalValue, LogicalValue>> entries) implements LogicalValue {}
    // tag is [0, 2^64-1] -- exceeds signed 64-bit, so it's kept as BigInteger
    // end to end rather than narrowed through a native long anywhere.
    record Tag(BigInteger tag, LogicalValue value) implements LogicalValue {}
    record Bignum(String sign, String value) implements LogicalValue {}

    final class ParseException extends RuntimeException {
        public ParseException(String message) { super(message); }
    }

    BigInteger U64_MAX = new BigInteger("18446744073709551615");

    static String requiredString(JsonNode obj, String field, String typeName) {
        JsonNode v = obj.get(field);
        if (v == null || v.isNull() || !v.isTextual()) {
            throw new ParseException(typeName + ": missing \"" + field + "\"");
        }
        return v.asText();
    }

    static LogicalValue parse(JsonNode json) {
        if (json == null || !json.isObject()) {
            throw new ParseException("expected a JSON object");
        }
        JsonNode typeNode = json.get("type");
        if (typeNode == null || typeNode.isNull() || !typeNode.isTextual()) {
            throw new ParseException("missing \"type\" field");
        }
        String t = typeNode.asText();
        switch (t) {
            case "int":
                return new Int(requiredString(json, "value", "int"));
            case "float":
                return new Flt(requiredString(json, "width", "float"), requiredString(json, "value", "float"));
            case "text":
                return new Text(requiredString(json, "value", "text"));
            case "bytes":
                return new Bytes(requiredString(json, "value", "bytes"));
            case "bool": {
                JsonNode v = json.get("value");
                if (v == null || v.isNull() || !v.isBoolean()) {
                    throw new ParseException("bool: missing \"value\"");
                }
                return new Bool(v.asBoolean());
            }
            case "null":
                return new Null();
            case "array": {
                JsonNode items = json.get("items");
                if (items == null || !items.isArray()) {
                    throw new ParseException("array: missing \"items\"");
                }
                List<LogicalValue> list = new ArrayList<>();
                for (JsonNode item : items) list.add(parse(item));
                return new Arr(list);
            }
            case "map": {
                JsonNode entries = json.get("entries");
                if (entries == null || !entries.isArray()) {
                    throw new ParseException("map: missing \"entries\"");
                }
                List<java.util.Map.Entry<LogicalValue, LogicalValue>> list = new ArrayList<>();
                for (JsonNode pair : entries) {
                    if (!pair.isArray() || pair.size() != 2) {
                        throw new ParseException("map entry must be a 2-element array");
                    }
                    list.add(java.util.Map.entry(parse(pair.get(0)), parse(pair.get(1))));
                }
                return new Map(list);
            }
            case "tag": {
                JsonNode tagNode = json.get("tag");
                if (tagNode == null || tagNode.isNull() || !tagNode.isIntegralNumber()) {
                    throw new ParseException("tag: missing \"tag\" number");
                }
                BigInteger big = tagNode.bigIntegerValue();
                if (big.signum() < 0 || big.compareTo(U64_MAX) > 0) {
                    throw new ParseException("tag: missing \"tag\" number");
                }
                JsonNode v = json.get("value");
                if (v == null || v.isNull()) {
                    throw new ParseException("tag: missing \"value\"");
                }
                return new Tag(big, parse(v));
            }
            case "bignum":
                return new Bignum(requiredString(json, "sign", "bignum"), requiredString(json, "value", "bignum"));
            default:
                throw new ParseException("unknown logical-value type: \"" + t + "\"");
        }
    }
}
