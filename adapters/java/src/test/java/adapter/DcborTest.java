package adapter;

import static org.junit.jupiter.api.Assertions.*;

import java.math.BigInteger;
import java.util.List;
import org.junit.jupiter.api.Test;

class DcborTest {
    private static LogicalValue.Flt f(String value) {
        return new LogicalValue.Flt("auto", value);
    }

    @Test
    void d5FloatWholeBecomesInt() {
        assertArrayEquals(Util.hexDecode("02"), Dcbor.encode(f("2.0")));
    }

    @Test
    void d5FloatFractionStaysFloat() {
        assertArrayEquals(Util.hexDecode("f94100"), Dcbor.encode(f("2.5")));
    }

    @Test
    void d6NanCanonicalPayload() {
        assertArrayEquals(Util.hexDecode("f97e00"), Dcbor.encode(f("NaN")));
    }

    @Test
    void d7ZeroAsInt() {
        assertArrayEquals(Util.hexDecode("00"), Dcbor.encode(new LogicalValue.Int("0")));
    }

    @Test
    void d7ZeroAsPosFloatUnifiesWithInt() {
        assertArrayEquals(Util.hexDecode("00"), Dcbor.encode(f("0.0")));
    }

    @Test
    void d7ZeroAsNegFloatUnifiesWithInt() {
        // Opposite of rfc8949's P7: -0.0 must also collapse to plain int 0.
        assertArrayEquals(Util.hexDecode("00"), Dcbor.encode(f("-0.0")));
    }

    @Test
    void d8NfcCombiningAccentNormalizes() {
        LogicalValue v = new LogicalValue.Text("café");
        assertArrayEquals(Util.hexDecode("65636166c3a9"), Dcbor.encode(v));
    }

    @Test
    void bignumBelowNativeRangeIsRejected() {
        LogicalValue value = new LogicalValue.Bignum("positive", "5");
        assertThrows(Rfc8949.EncodeException.class, () -> Dcbor.encode(value));
    }

    @Test
    void bignumAt2Pow64UsesTag2() {
        LogicalValue value = new LogicalValue.Bignum("positive", "18446744073709551616");
        assertArrayEquals(Util.hexDecode("c249010000000000000000"), Dcbor.encode(value));
    }

    @Test
    void tagWrapsInnerValue() {
        LogicalValue value = new LogicalValue.Tag(BigInteger.valueOf(100), new LogicalValue.Int("5"));
        assertArrayEquals(new byte[] { (byte) 0xd8, 0x64, 0x05 }, Dcbor.encode(value));
    }

    @Test
    void reservedBignumTagViaGenericTagArmIsARawPassthrough() {
        LogicalValue value = new LogicalValue.Tag(BigInteger.valueOf(2), new LogicalValue.Bytes("01"));
        assertArrayEquals(new byte[] { (byte) 0xc2, 0x41, 0x01 }, Dcbor.encode(value));
    }

    @Test
    void explicitWidthIsIgnoredWholeFloatStillReduces() {
        LogicalValue v = new LogicalValue.Flt("f64", "2.0");
        assertArrayEquals(Util.hexDecode("02"), Dcbor.encode(v));
    }

    @Test
    void explicitWidthIsIgnoredNanStillCanonical() {
        LogicalValue v = new LogicalValue.Flt("f32", "NaN");
        assertArrayEquals(Util.hexDecode("f97e00"), Dcbor.encode(v));
    }

    @Test
    void tagAtU64MaxDoesNotTruncate() {
        LogicalValue value = new LogicalValue.Tag(new BigInteger("18446744073709551615"), new LogicalValue.Int("5"));
        assertArrayEquals(Util.hexDecode("dbffffffffffffffff05"), Dcbor.encode(value));
    }

    @Test
    void mapKeyCollisionDedupesLastWriteWins() {
        // D7: 0, 0.0, and -0.0 all canonicalize to encoded key 0x00, so they
        // collapse to one entry, keeping only the last value written (3).
        LogicalValue value = new LogicalValue.Map(List.of(
            java.util.Map.entry(new LogicalValue.Int("0"), new LogicalValue.Int("1")),
            java.util.Map.entry(f("0.0"), new LogicalValue.Int("2")),
            java.util.Map.entry(f("-0.0"), new LogicalValue.Int("3"))));
        assertArrayEquals(Util.hexDecode("a10003"), Dcbor.encode(value));
    }

    @Test
    void mapKeyCollisionViaNfcDedupesLastWriteWins() {
        // D8: "café" (precomposed) and "café" (NFC-decomposed) both normalize
        // to the identical encoded key bytes, so they must also collapse to a
        // single entry, last write wins.
        String decomposed = "café"; // "e" + combining acute accent
        String precomposed = "café"; // single precomposed char
        LogicalValue value = new LogicalValue.Map(List.of(
            java.util.Map.entry(new LogicalValue.Text(decomposed), new LogicalValue.Int("1")),
            java.util.Map.entry(new LogicalValue.Text(precomposed), new LogicalValue.Int("2"))));
        assertArrayEquals(Util.hexDecode("a165636166c3a902"), Dcbor.encode(value));
    }

    @Test
    void arrayAndMapRoundTrip() {
        LogicalValue.Arr arr = new LogicalValue.Arr(List.of(new LogicalValue.Int("1"), new LogicalValue.Int("2")));
        assertArrayEquals(new byte[] { (byte) 0x82, 0x01, 0x02 }, Dcbor.encode(arr));

        LogicalValue map = new LogicalValue.Map(
            List.of(java.util.Map.entry(new LogicalValue.Text("a"), new LogicalValue.Int("1"))));
        assertArrayEquals(Util.hexDecode("a1616101"), Dcbor.encode(map));
    }
}
