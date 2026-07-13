package adapter;

import java.io.ByteArrayOutputStream;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** RFC 8949 core deterministic encoding (P1-P9). */
public final class Rfc8949 {
    private Rfc8949() {}

    public static final class EncodeException extends RuntimeException {
        public EncodeException(String message) { super(message); }
    }

    static final BigInteger TWO_POW_64 = BigInteger.ONE.shiftLeft(64);

    private static byte[] bigIntToBytes(BigInteger v, int width) {
        byte[] raw = v.toByteArray();
        byte[] unsigned = (raw.length > 1 && raw[0] == 0) ? java.util.Arrays.copyOfRange(raw, 1, raw.length) : raw;
        byte[] out = new byte[width];
        System.arraycopy(unsigned, 0, out, width - unsigned.length, unsigned.length);
        return out;
    }

    static byte[] stripLeadingZeros(byte[] bytes) {
        int start = 0;
        while (start < bytes.length - 1 && bytes[start] == 0) start++;
        return java.util.Arrays.copyOfRange(bytes, start, bytes.length);
    }

    private static byte[] concat(byte[] a, byte[] b) {
        byte[] out = new byte[a.length + b.length];
        System.arraycopy(a, 0, out, 0, a.length);
        System.arraycopy(b, 0, out, a.length, b.length);
        return out;
    }

    public static byte[] encodeHead(int majorType, BigInteger arg) {
        int mt = majorType << 5;
        if (arg.compareTo(BigInteger.valueOf(24)) < 0) {
            return new byte[] { (byte) (mt | arg.intValue()) };
        } else if (arg.compareTo(BigInteger.valueOf(0xff)) <= 0) {
            return concat(new byte[] { (byte) (mt | 24) }, bigIntToBytes(arg, 1));
        } else if (arg.compareTo(BigInteger.valueOf(0xffff)) <= 0) {
            return concat(new byte[] { (byte) (mt | 25) }, bigIntToBytes(arg, 2));
        } else if (arg.compareTo(BigInteger.valueOf(0xffffffffL)) <= 0) {
            return concat(new byte[] { (byte) (mt | 26) }, bigIntToBytes(arg, 4));
        } else {
            return concat(new byte[] { (byte) (mt | 27) }, bigIntToBytes(arg, 8));
        }
    }

    /** Shared with Dcbor (same shortest-int rule applies to both profiles). */
    public static byte[] encodeInt(String value) {
        BigInteger n;
        try {
            n = new BigInteger(value);
        } catch (NumberFormatException e) {
            throw new EncodeException("int: invalid integer literal \"" + value + "\": " + e.getMessage());
        }
        if (n.signum() >= 0) {
            if (n.compareTo(TWO_POW_64) >= 0) throw new EncodeException("int: " + n + " exceeds native range");
            return encodeHead(0, n);
        } else {
            BigInteger arg = BigInteger.valueOf(-1).subtract(n);
            if (arg.compareTo(TWO_POW_64) >= 0) throw new EncodeException("int: " + n + " exceeds native range");
            return encodeHead(1, arg);
        }
    }

    /** Shared with Dcbor. */
    public static byte[] doubleToBeBytes(double f) {
        long bits = Double.doubleToRawLongBits(f);
        byte[] out = new byte[8];
        for (int i = 0; i < 8; i++) out[i] = (byte) ((bits >>> (56 - 8 * i)) & 0xffL);
        return out;
    }

    private static double parseFloatLiteral(String literal) {
        try {
            return Double.parseDouble(literal);
        } catch (NumberFormatException e) {
            throw new EncodeException("float: invalid literal \"" + literal + "\"");
        }
    }

    private static byte[] encodeFloat(String width, String literal) {
        double f = parseFloatLiteral(literal);
        if (Double.isNaN(f)) {
            // P6: NaN always uses the single canonical f16 payload, regardless of width.
            return new byte[] { (byte) 0xf9, 0x7e, 0x00 };
        }
        if (width.equals("auto")) {
            byte[] f16 = Float16.tryF16(f);
            if (f16 != null) return concat(new byte[] { (byte) 0xf9 }, f16);
            byte[] f32 = Float16.tryF32(f);
            if (f32 != null) return concat(new byte[] { (byte) 0xfa }, f32);
            return concat(new byte[] { (byte) 0xfb }, doubleToBeBytes(f));
        }
        switch (width) {
            case "f16": {
                byte[] f16 = Float16.tryF16(f);
                if (f16 == null) throw new EncodeException("float: " + f + " cannot round-trip at requested width f16");
                return concat(new byte[] { (byte) 0xf9 }, f16);
            }
            case "f32": {
                byte[] f32 = Float16.tryF32(f);
                if (f32 == null) throw new EncodeException("float: " + f + " cannot round-trip at requested width f32");
                return concat(new byte[] { (byte) 0xfa }, f32);
            }
            case "f64":
                return concat(new byte[] { (byte) 0xfb }, doubleToBeBytes(f));
            default:
                throw new EncodeException("float: unknown width \"" + width + "\"");
        }
    }

    /** Shared with Dcbor: bignum tag+magnitude rules are identical in both profiles. */
    public static Map.Entry<Integer, byte[]> bignumTagAndBytes(String sign, String value) {
        BigInteger magnitude;
        try {
            magnitude = new BigInteger(value);
        } catch (NumberFormatException e) {
            throw new EncodeException("bignum: invalid magnitude \"" + value + "\": " + e.getMessage());
        }
        if (magnitude.compareTo(TWO_POW_64) < 0) {
            int tag = sign.equals("positive") ? 2 : 3;
            throw new EncodeException(
                "bignum magnitude " + magnitude + " fits in a native CBOR integer and must not be encoded as tag "
                    + tag + " (SPEC.md bignum rule)");
        }
        BigInteger rawInt;
        switch (sign) {
            case "positive": rawInt = magnitude; break;
            case "negative": rawInt = magnitude.subtract(BigInteger.ONE); break;
            default: throw new EncodeException("bignum: unknown sign \"" + sign + "\"");
        }
        int tag = sign.equals("positive") ? 2 : 3;
        byte[] bytes = stripLeadingZeros(rawInt.toByteArray());
        return new AbstractMap.SimpleEntry<>(tag, bytes);
    }

    private static byte[] encodeBignum(String sign, String value) {
        Map.Entry<Integer, byte[]> tagAndBytes = bignumTagAndBytes(sign, value);
        byte[] out = encodeHead(6, BigInteger.valueOf(tagAndBytes.getKey()));
        out = concat(out, encodeHead(2, BigInteger.valueOf(tagAndBytes.getValue().length)));
        out = concat(out, tagAndBytes.getValue());
        return out;
    }

    /** Shared with Dcbor (same bytewise map-key sort rule applies to both profiles). */
    public static int compareBytesUnsigned(byte[] a, byte[] b) {
        int len = Math.min(a.length, b.length);
        for (int i = 0; i < len; i++) {
            int ai = a[i] & 0xff;
            int bi = b[i] & 0xff;
            if (ai != bi) return ai - bi;
        }
        return a.length - b.length;
    }

    public static byte[] encode(LogicalValue value) {
        if (value instanceof LogicalValue.Int v) {
            return encodeInt(v.value());
        } else if (value instanceof LogicalValue.Flt v) {
            return encodeFloat(v.width(), v.value());
        } else if (value instanceof LogicalValue.Text v) {
            byte[] bytes = v.value().getBytes(StandardCharsets.UTF_8);
            return concat(encodeHead(3, BigInteger.valueOf(bytes.length)), bytes);
        } else if (value instanceof LogicalValue.Bytes v) {
            byte[] bytes = Util.hexDecode(v.value());
            return concat(encodeHead(2, BigInteger.valueOf(bytes.length)), bytes);
        } else if (value instanceof LogicalValue.Bool v) {
            return new byte[] { v.value() ? (byte) 0xf5 : (byte) 0xf4 };
        } else if (value instanceof LogicalValue.Null) {
            return new byte[] { (byte) 0xf6 };
        } else if (value instanceof LogicalValue.Arr v) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] head = encodeHead(4, BigInteger.valueOf(v.items().size()));
            out.writeBytes(head);
            for (LogicalValue item : v.items()) out.writeBytes(encode(item));
            return out.toByteArray();
        } else if (value instanceof LogicalValue.Map v) {
            List<Map.Entry<byte[], byte[]>> encoded = new ArrayList<>();
            for (Map.Entry<LogicalValue, LogicalValue> e : v.entries()) {
                encoded.add(new AbstractMap.SimpleEntry<>(encode(e.getKey()), encode(e.getValue())));
            }
            encoded.sort((a, b) -> compareBytesUnsigned(a.getKey(), b.getKey()));
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            out.writeBytes(encodeHead(5, BigInteger.valueOf(v.entries().size())));
            for (Map.Entry<byte[], byte[]> e : encoded) {
                out.writeBytes(e.getKey());
                out.writeBytes(e.getValue());
            }
            return out.toByteArray();
        } else if (value instanceof LogicalValue.Tag v) {
            return concat(encodeHead(6, v.tag()), encode(v.value()));
        } else if (value instanceof LogicalValue.Bignum v) {
            return encodeBignum(v.sign(), v.value());
        }
        throw new IllegalStateException("unreachable: sealed LogicalValue");
    }
}
