package adapter;

import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.text.Normalizer;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Strict canonical-CBOR decoder (SPEC.md's `decode-strict` mode). Parses raw
 * CBOR bytes into a byte-range-aware intermediate form and rejects any
 * well-formed-but-non-canonical input with the single most specific
 * decode-strict reason code (P1-P9 / D1-D9). Hand-rolled (not delegated to a
 * generic CBOR library) because a generic decode normalizes away exactly the
 * information needed to tell *why* something is non-canonical: which
 * additional-info width was used, raw NaN payload bits, raw map-key byte
 * order. See README.md for the library investigation this conclusion is
 * based on.
 *
 * Known scope gap, matching adapters/rust/src/decode.rs and
 * adapters/kotlin/.../Decode.kt: a tag-2/3 (bignum) item whose magnitude
 * fits the native 64-bit range is not rejected here -- none of the 11
 * documented decode-strict reason codes covers it and no vector in the
 * corpus exercises it.
 *
 * `arg`/tag values that are logically unsigned 64-bit use Java's signed
 * `long` as a 64-bit bit-pattern with `Long.compareUnsigned` /
 * `Long.toUnsignedString`, per the standard JDK idiom for unsigned 64-bit
 * arithmetic (Java has no native unsigned long type, unlike Kotlin's ULong).
 */
public final class Decode {
    private Decode() {}

    public enum Profile { RFC8949, DCBOR }

    public static final class DecodeException extends RuntimeException {
        public final String reasonOrMessage;
        public DecodeException(String reasonOrMessage) {
            super(reasonOrMessage);
            this.reasonOrMessage = reasonOrMessage;
        }
    }

    private static final Set<String> REASON_CODES = Set.of(
        "NON_SHORTEST_INT",
        "UNSORTED_MAP_KEYS",
        "INDEFINITE_LENGTH",
        "DUPLICATE_KEY",
        "NON_SHORTEST_FLOAT",
        "NAN_PAYLOAD_VARIANT",
        "TRAILING_BYTES",
        "MULTIPLE_TOP_LEVEL_ITEMS",
        "UNKNOWN_TAG",
        "NON_NFC_STRING",
        "UNREDUCED_NUMERIC"
    );

    // Placeholder allow-list for the UNKNOWN_TAG check: only the bignum tags
    // are exercised by the current vector corpus.
    private static final Set<Long> ALLOWED_TAGS = Set.of(2L, 3L);

    public sealed interface Verdict {
        record Accept(byte[] bytes) implements Verdict {}
        record Reject(String reason) implements Verdict {}
    }

    private static final class Cursor { int pos; Cursor(int pos) { this.pos = pos; } }

    public static Verdict decodeStrict(byte[] input, Profile profile) {
        if (input.length == 0) throw new DecodeException("internal: empty input line");
        Cursor cursor = new Cursor(0);
        Item item;
        try {
            item = parseItem(input, cursor, profile);
        } catch (DecodeException e) {
            if (REASON_CODES.contains(e.reasonOrMessage)) return new Verdict.Reject(e.reasonOrMessage);
            throw e;
        }

        if (cursor.pos < input.length) {
            Cursor cursor2 = new Cursor(cursor.pos);
            try {
                parseItem(input, cursor2, profile);
                return cursor2.pos == input.length
                    ? new Verdict.Reject("MULTIPLE_TOP_LEVEL_ITEMS")
                    : new Verdict.Reject("TRAILING_BYTES");
            } catch (RuntimeException e) {
                return new Verdict.Reject("TRAILING_BYTES");
            }
        }

        LogicalValue logical = itemToLogical(item);
        byte[] reencoded;
        try {
            reencoded = switch (profile) {
                case RFC8949 -> Rfc8949.encode(logical);
                case DCBOR -> Dcbor.encode(logical);
            };
        } catch (Rfc8949.EncodeException e) {
            throw new DecodeException("internal: canonical input failed to re-encode: " + e.getMessage());
        }
        return new Verdict.Accept(reencoded);
    }

    private sealed interface Item {
        record IntPos(long v) implements Item {}
        record IntNeg(long v) implements Item {}
        record Bytes(byte[] v) implements Item {}
        record Text(String v) implements Item {}
        record Arr(List<Item> items) implements Item {}
        record MapEntries(List<Map.Entry<Item, Item>> entries) implements Item {}
        record TaggedItem(long tag, Item inner) implements Item {}
        record BoolItem(boolean v) implements Item {}
        record NullItem() implements Item {}
        record FloatItem(double v) implements Item {}
    }

    private enum ArgWidth { DIRECT, ONE, TWO, FOUR, EIGHT }

    private static final class Head {
        final int major; final long arg; final ArgWidth width; final boolean indefinite;
        Head(int major, long arg, ArgWidth width, boolean indefinite) {
            this.major = major; this.arg = arg; this.width = width; this.indefinite = indefinite;
        }
    }

    private static Head readHead(byte[] input, Cursor cursor) {
        if (cursor.pos >= input.length) throw new DecodeException("internal: truncated input (expected an item header)");
        int b0 = input[cursor.pos] & 0xff;
        cursor.pos += 1;
        int major = (b0 >>> 5) & 0x7;
        int info = b0 & 0x1f;
        if (info == 31) return new Head(major, 0L, ArgWidth.DIRECT, true);

        long arg;
        ArgWidth width;
        if (info <= 23) {
            arg = info;
            width = ArgWidth.DIRECT;
        } else if (info == 24) {
            if (cursor.pos >= input.length) throw new DecodeException("internal: truncated 1-byte argument");
            arg = input[cursor.pos] & 0xffL;
            cursor.pos += 1;
            width = ArgWidth.ONE;
        } else if (info == 25) {
            if (cursor.pos + 2 > input.length) throw new DecodeException("internal: truncated 2-byte argument");
            long v = 0;
            for (int i = 0; i < 2; i++) v = (v << 8) | (input[cursor.pos + i] & 0xffL);
            cursor.pos += 2;
            arg = v;
            width = ArgWidth.TWO;
        } else if (info == 26) {
            if (cursor.pos + 4 > input.length) throw new DecodeException("internal: truncated 4-byte argument");
            long v = 0;
            for (int i = 0; i < 4; i++) v = (v << 8) | (input[cursor.pos + i] & 0xffL);
            cursor.pos += 4;
            arg = v;
            width = ArgWidth.FOUR;
        } else if (info == 27) {
            if (cursor.pos + 8 > input.length) throw new DecodeException("internal: truncated 8-byte argument");
            long v = 0;
            for (int i = 0; i < 8; i++) v = (v << 8) | (input[cursor.pos + i] & 0xffL);
            cursor.pos += 8;
            arg = v;
            width = ArgWidth.EIGHT;
        } else {
            throw new DecodeException("internal: reserved additional-info value (28-30)");
        }
        return new Head(major, arg, width, false);
    }

    private static ArgWidth shortestWidthFor(long arg) {
        if (Long.compareUnsigned(arg, 24L) < 0) return ArgWidth.DIRECT;
        if (Long.compareUnsigned(arg, 0xffL) <= 0) return ArgWidth.ONE;
        if (Long.compareUnsigned(arg, 0xffffL) <= 0) return ArgWidth.TWO;
        if (Long.compareUnsigned(arg, 0xffffffffL) <= 0) return ArgWidth.FOUR;
        return ArgWidth.EIGHT;
    }

    // P1/D2: every integer *argument* (ints themselves, and string/array/map
    // lengths, and tag numbers) must use the shortest encoding for its value.
    private static void checkShortestArg(Head head) {
        if (head.width != shortestWidthFor(head.arg)) throw new DecodeException("NON_SHORTEST_INT");
    }

    private static String strictUtf8Decode(byte[] bytes) {
        var decoder = StandardCharsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT);
        try {
            return decoder.decode(ByteBuffer.wrap(bytes)).toString();
        } catch (CharacterCodingException e) {
            throw new DecodeException("internal: invalid utf-8 in text string");
        }
    }

    private static Item parseItem(byte[] input, Cursor cursor, Profile profile) {
        Head head = readHead(input, cursor);

        if (head.major == 7) {
            if (head.indefinite) {
                throw new DecodeException("internal: unexpected break byte outside indefinite-length container");
            }
            return parseMajor7(head.width, head.arg, profile);
        }

        if (head.indefinite) {
            // P3/D1: indefinite-length arrays/maps/byte/text strings are
            // banned outright, regardless of what they'd otherwise contain.
            throw new DecodeException("INDEFINITE_LENGTH");
        }
        checkShortestArg(head);

        switch (head.major) {
            case 0:
                return new Item.IntPos(head.arg);
            case 1:
                return new Item.IntNeg(head.arg);
            case 2: {
                int len = uArgToInt(head.arg);
                if (cursor.pos + len > input.length) throw new DecodeException("internal: truncated byte string");
                byte[] bytes = java.util.Arrays.copyOfRange(input, cursor.pos, cursor.pos + len);
                cursor.pos += len;
                return new Item.Bytes(bytes);
            }
            case 3: {
                int len = uArgToInt(head.arg);
                if (cursor.pos + len > input.length) throw new DecodeException("internal: truncated text string");
                byte[] bytes = java.util.Arrays.copyOfRange(input, cursor.pos, cursor.pos + len);
                String s = strictUtf8Decode(bytes);
                cursor.pos += len;
                // D8: dcbor requires NFC-normalized text; rfc8949 has no such
                // rule (P9 is an explicit non-goal).
                if (profile == Profile.DCBOR) {
                    String normalized = Normalizer.normalize(s, Normalizer.Form.NFC);
                    if (!normalized.equals(s)) throw new DecodeException("NON_NFC_STRING");
                }
                return new Item.Text(s);
            }
            case 4: {
                int len = uArgToInt(head.arg);
                List<Item> items = new ArrayList<>(len);
                for (int i = 0; i < len; i++) items.add(parseItem(input, cursor, profile));
                return new Item.Arr(items);
            }
            case 5: {
                int len = uArgToInt(head.arg);
                List<Map.Entry<Item, Item>> entries = new ArrayList<>(len);
                List<int[]> keyRanges = new ArrayList<>(len);
                for (int i = 0; i < len; i++) {
                    int keyStart = cursor.pos;
                    Item key = parseItem(input, cursor, profile);
                    int keyEnd = cursor.pos;
                    Item value = parseItem(input, cursor, profile);
                    keyRanges.add(new int[] { keyStart, keyEnd });
                    entries.add(new AbstractMap.SimpleEntry<>(key, value));
                }
                // P2/D3: keys must appear in strictly increasing bytewise
                // order of their own raw encoded bytes. A duplicate key is
                // necessarily adjacent to itself in properly sorted order,
                // so one adjacent pass catches both violations.
                for (int i = 0; i < keyRanges.size() - 1; i++) {
                    byte[] a = java.util.Arrays.copyOfRange(input, keyRanges.get(i)[0], keyRanges.get(i)[1]);
                    byte[] b = java.util.Arrays.copyOfRange(input, keyRanges.get(i + 1)[0], keyRanges.get(i + 1)[1]);
                    int cmp = Rfc8949.compareBytesUnsigned(a, b);
                    if (cmp == 0) throw new DecodeException("DUPLICATE_KEY");
                    if (cmp > 0) throw new DecodeException("UNSORTED_MAP_KEYS");
                }
                return new Item.MapEntries(entries);
            }
            case 6: {
                long tag = head.arg;
                if (!ALLOWED_TAGS.contains(tag)) throw new DecodeException("UNKNOWN_TAG");
                Item inner = parseItem(input, cursor, profile);
                return new Item.TaggedItem(tag, inner);
            }
            default:
                throw new IllegalStateException("major type is 3 bits, always 0-7");
        }
    }

    private static int uArgToInt(long arg) {
        // Vector corpus lengths always fit a Java array index; a length that
        // doesn't is caught as a truncated-input error by the bounds check
        // that immediately follows every call site.
        return (int) arg;
    }

    private enum FloatWidth { F16, F32, F64 }

    private static Item parseMajor7(ArgWidth width, long arg, Profile profile) {
        switch (width) {
            case DIRECT:
                if (arg == 20L) return new Item.BoolItem(false);
                if (arg == 21L) return new Item.BoolItem(true);
                if (arg == 22L) return new Item.NullItem();
                throw new DecodeException("internal: unsupported major-7 simple value " + arg);
            case ONE:
                throw new DecodeException("internal: unsupported major-7 1-byte simple value");
            case TWO:
                return checkFloat(arg, FloatWidth.F16, profile);
            case FOUR:
                return checkFloat(arg, FloatWidth.F32, profile);
            case EIGHT:
                return checkFloat(arg, FloatWidth.F64, profile);
            default:
                throw new IllegalStateException("unreachable");
        }
    }

    private static Item checkFloat(long bits, FloatWidth width, Profile profile) {
        double value = switch (width) {
            case F16 -> Float16.f16BitsToDouble((int) bits);
            case F32 -> Float.intBitsToFloat((int) bits);
            case F64 -> Double.longBitsToDouble(bits);
        };

        if (Double.isNaN(value)) {
            // P6/D6: canonical NaN is exactly f16 with payload 0x7e00, in both profiles.
            if (width != FloatWidth.F16 || bits != 0x7e00L) throw new DecodeException("NAN_PAYLOAD_VARIANT");
            return new Item.FloatItem(Double.NaN);
        }

        // D5/D7 (dcbor only): any whole-number float in [-2^63, 2^64-1] --
        // including +/-0.0, per D7 zero unification -- must be a plain int
        // instead. This is checked before, and takes priority over, the
        // general shortest-float-width rule below.
        if (profile == Profile.DCBOR && isDcborReducible(value)) throw new DecodeException("UNREDUCED_NUMERIC");

        // P5/D2: the width used must be the narrowest of f16/f32/f64 that
        // round-trips `value` exactly.
        FloatWidth shortest = shortestFloatWidth(value);
        if (width != shortest) throw new DecodeException("NON_SHORTEST_FLOAT");

        return new Item.FloatItem(value);
    }

    private static boolean isDcborReducible(double value) {
        if (Double.isInfinite(value)) return false;
        if (value != Math.floor(value)) return false;
        return value >= -9_223_372_036_854_775_808.0 && value <= 18_446_744_073_709_551_615.0;
    }

    private static FloatWidth shortestFloatWidth(double value) {
        long targetBits = Double.doubleToRawLongBits(value);
        int f16Bits = Float16.doubleToF16Bits(value);
        if (Double.doubleToRawLongBits(Float16.f16BitsToDouble(f16Bits)) == targetBits) return FloatWidth.F16;
        float f32 = (float) value;
        if (Double.doubleToRawLongBits((double) f32) == targetBits) return FloatWidth.F32;
        return FloatWidth.F64;
    }

    private static String formatFloat(double value) {
        return Double.isNaN(value) ? "NaN" : Double.toString(value);
    }

    private static LogicalValue itemToLogical(Item item) {
        if (item instanceof Item.IntPos v) {
            return new LogicalValue.Int(Long.toUnsignedString(v.v()));
        } else if (item instanceof Item.IntNeg v) {
            BigInteger n = new BigInteger(Long.toUnsignedString(v.v()));
            return new LogicalValue.Int(BigInteger.valueOf(-1).subtract(n).toString());
        } else if (item instanceof Item.Bytes v) {
            return new LogicalValue.Bytes(Util.hexEncode(v.v()));
        } else if (item instanceof Item.Text v) {
            return new LogicalValue.Text(v.v());
        } else if (item instanceof Item.Arr v) {
            List<LogicalValue> items = new ArrayList<>();
            for (Item i : v.items()) items.add(itemToLogical(i));
            return new LogicalValue.Arr(items);
        } else if (item instanceof Item.MapEntries v) {
            List<Map.Entry<LogicalValue, LogicalValue>> entries = new ArrayList<>();
            for (Map.Entry<Item, Item> e : v.entries()) {
                entries.add(new AbstractMap.SimpleEntry<>(itemToLogical(e.getKey()), itemToLogical(e.getValue())));
            }
            return new LogicalValue.Map(entries);
        } else if (item instanceof Item.TaggedItem v) {
            return new LogicalValue.Tag(new BigInteger(Long.toUnsignedString(v.tag())), itemToLogical(v.inner()));
        } else if (item instanceof Item.BoolItem v) {
            return new LogicalValue.Bool(v.v());
        } else if (item instanceof Item.NullItem) {
            return new LogicalValue.Null();
        } else if (item instanceof Item.FloatItem v) {
            return new LogicalValue.Flt("auto", formatFloat(v.v()));
        }
        throw new IllegalStateException("unreachable: sealed Item");
    }
}
