package adapter;

import java.io.ByteArrayOutputStream;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.text.Normalizer;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * dCBOR profile: no JVM library implements draft-mcnally-deterministic-cbor,
 * so D5 (numeric reduction), D6 (NaN pin), D7 (zero unification), and D8 (NFC
 * normalization) are reimplemented directly here. D3 (map key sort) and
 * bignum handling reuse the exact same rules as rfc8949 (see Rfc8949.java) --
 * do not fork that logic per profile, only the parts SPEC.md says diverge.
 */
public final class Dcbor {
    private Dcbor() {}

    private static byte[] concat(byte[] a, byte[] b) {
        byte[] out = new byte[a.length + b.length];
        System.arraycopy(a, 0, out, 0, a.length);
        System.arraycopy(b, 0, out, a.length, b.length);
        return out;
    }

    private static byte[] encodeFloat(String literal) {
        double f;
        try {
            f = Double.parseDouble(literal);
        } catch (NumberFormatException e) {
            throw new Rfc8949.EncodeException("float: invalid literal \"" + literal + "\"");
        }
        if (Double.isNaN(f)) {
            // D6: NaN always uses the single canonical f16 payload.
            return new byte[] { (byte) 0xf9, 0x7e, 0x00 };
        }
        if (!Double.isInfinite(f) && f == Math.floor(f)) {
            // D5/D7: a float with no fractional part (including -0.0) reduces
            // to the shortest-int form, unifying with the plain integer
            // encoding.
            BigInteger big = new BigDecimal(f).toBigInteger();
            if (big.signum() >= 0) {
                if (big.compareTo(Rfc8949.TWO_POW_64) < 0) return Rfc8949.encodeHead(0, big);
            } else {
                BigInteger arg = BigInteger.valueOf(-1).subtract(big);
                if (arg.compareTo(Rfc8949.TWO_POW_64) < 0) return Rfc8949.encodeHead(1, arg);
            }
            // Falls through to shortest-float form below if it doesn't fit
            // the native int range (no dcbor vector exercises this edge).
        }
        byte[] f16 = Float16.tryF16(f);
        if (f16 != null) return concat(new byte[] { (byte) 0xf9 }, f16);
        byte[] f32 = Float16.tryF32(f);
        if (f32 != null) return concat(new byte[] { (byte) 0xfa }, f32);
        return concat(new byte[] { (byte) 0xfb }, Rfc8949.doubleToBeBytes(f));
    }

    private static byte[] encodeBignum(String sign, String value) {
        Map.Entry<Integer, byte[]> tagAndBytes = Rfc8949.bignumTagAndBytes(sign, value);
        byte[] out = Rfc8949.encodeHead(6, BigInteger.valueOf(tagAndBytes.getKey()));
        out = concat(out, Rfc8949.encodeHead(2, BigInteger.valueOf(tagAndBytes.getValue().length)));
        out = concat(out, tagAndBytes.getValue());
        return out;
    }

    public static byte[] encode(LogicalValue value) {
        if (value instanceof LogicalValue.Int v) {
            return Rfc8949.encodeInt(v.value());
        } else if (value instanceof LogicalValue.Flt v) {
            return encodeFloat(v.value());
        } else if (value instanceof LogicalValue.Text v) {
            // D8: normalize to NFC before UTF-8 encoding.
            byte[] bytes = Normalizer.normalize(v.value(), Normalizer.Form.NFC).getBytes(StandardCharsets.UTF_8);
            return concat(Rfc8949.encodeHead(3, BigInteger.valueOf(bytes.length)), bytes);
        } else if (value instanceof LogicalValue.Bytes v) {
            byte[] bytes = Util.hexDecode(v.value());
            return concat(Rfc8949.encodeHead(2, BigInteger.valueOf(bytes.length)), bytes);
        } else if (value instanceof LogicalValue.Bool v) {
            return new byte[] { v.value() ? (byte) 0xf5 : (byte) 0xf4 };
        } else if (value instanceof LogicalValue.Null) {
            return new byte[] { (byte) 0xf6 };
        } else if (value instanceof LogicalValue.Arr v) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            out.writeBytes(Rfc8949.encodeHead(4, BigInteger.valueOf(v.items().size())));
            for (LogicalValue item : v.items()) out.writeBytes(encode(item));
            return out.toByteArray();
        } else if (value instanceof LogicalValue.Map v) {
            // dCBOR's reference encoder stores entries in a real map keyed by
            // canonical-encoded bytes, so two logical entries that
            // canonicalize to the same key (via D7 zero unification or D8
            // NFC normalization) collapse to one, last write wins. Mirror
            // that with a LinkedHashMap keyed by hex(key) -- do NOT sort
            // before collapsing.
            LinkedHashMap<String, Map.Entry<byte[], byte[]>> dedup = new LinkedHashMap<>();
            for (Map.Entry<LogicalValue, LogicalValue> e : v.entries()) {
                byte[] encodedKey = encode(e.getKey());
                dedup.put(Util.hexEncode(encodedKey), new AbstractMap.SimpleEntry<>(encodedKey, encode(e.getValue())));
            }
            List<Map.Entry<byte[], byte[]>> sorted = new ArrayList<>(dedup.values());
            sorted.sort((a, b) -> Rfc8949.compareBytesUnsigned(a.getKey(), b.getKey()));
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            out.writeBytes(Rfc8949.encodeHead(5, BigInteger.valueOf(sorted.size())));
            for (Map.Entry<byte[], byte[]> e : sorted) {
                out.writeBytes(e.getKey());
                out.writeBytes(e.getValue());
            }
            return out.toByteArray();
        } else if (value instanceof LogicalValue.Tag v) {
            return concat(Rfc8949.encodeHead(6, v.tag()), encode(v.value()));
        } else if (value instanceof LogicalValue.Bignum v) {
            return encodeBignum(v.sign(), v.value());
        }
        throw new IllegalStateException("unreachable: sealed LogicalValue");
    }
}
