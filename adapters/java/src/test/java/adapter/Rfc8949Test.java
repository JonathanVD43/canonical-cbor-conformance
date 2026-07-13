package adapter;

import static org.junit.jupiter.api.Assertions.*;

import java.math.BigInteger;
import java.util.List;
import org.junit.jupiter.api.Test;

class Rfc8949Test {
    @Test
    void shortestIntZero() {
        assertArrayEquals(new byte[] { 0x00 }, Rfc8949.encode(new LogicalValue.Int("0")));
    }

    @Test
    void shortestIntBoundary23_24() {
        assertArrayEquals(new byte[] { 0x17 }, Rfc8949.encode(new LogicalValue.Int("23")));
        assertArrayEquals(new byte[] { 0x18, 0x18 }, Rfc8949.encode(new LogicalValue.Int("24")));
    }

    @Test
    void shortestIntBoundary255_256() {
        assertArrayEquals(new byte[] { 0x18, (byte) 0xff }, Rfc8949.encode(new LogicalValue.Int("255")));
        assertArrayEquals(new byte[] { 0x19, 0x01, 0x00 }, Rfc8949.encode(new LogicalValue.Int("256")));
    }

    @Test
    void shortestIntBoundary65535_65536() {
        assertArrayEquals(new byte[] { 0x19, (byte) 0xff, (byte) 0xff }, Rfc8949.encode(new LogicalValue.Int("65535")));
        assertArrayEquals(
            new byte[] { 0x1a, 0x00, 0x01, 0x00, 0x00 }, Rfc8949.encode(new LogicalValue.Int("65536")));
    }

    @Test
    void shortestIntBoundary4294967295_4294967296() {
        assertArrayEquals(
            new byte[] { 0x1a, (byte) 0xff, (byte) 0xff, (byte) 0xff, (byte) 0xff },
            Rfc8949.encode(new LogicalValue.Int("4294967295")));
        assertArrayEquals(Util.hexDecode("1b0000000100000000"), Rfc8949.encode(new LogicalValue.Int("4294967296")));
    }

    @Test
    void negativeInt() {
        assertArrayEquals(new byte[] { 0x20 }, Rfc8949.encode(new LogicalValue.Int("-1")));
    }

    @Test
    void negativeIntMinI64Boundary() {
        assertArrayEquals(
            Util.hexDecode("3b7fffffffffffffff"), Rfc8949.encode(new LogicalValue.Int("-9223372036854775808")));
    }

    @Test
    void textAndBytes() {
        assertArrayEquals(new byte[] { 0x61, 'a' }, Rfc8949.encode(new LogicalValue.Text("a")));
        assertArrayEquals(
            new byte[] { 0x44, (byte) 0xde, (byte) 0xad, (byte) 0xbe, (byte) 0xef },
            Rfc8949.encode(new LogicalValue.Bytes("deadbeef")));
    }

    @Test
    void boolAndNull() {
        assertArrayEquals(new byte[] { (byte) 0xf5 }, Rfc8949.encode(new LogicalValue.Bool(true)));
        assertArrayEquals(new byte[] { (byte) 0xf4 }, Rfc8949.encode(new LogicalValue.Bool(false)));
        assertArrayEquals(new byte[] { (byte) 0xf6 }, Rfc8949.encode(new LogicalValue.Null()));
    }

    @Test
    void arrayOfInts() {
        LogicalValue.Arr arr = new LogicalValue.Arr(List.of(new LogicalValue.Int("1"), new LogicalValue.Int("2")));
        assertArrayEquals(new byte[] { (byte) 0x82, 0x01, 0x02 }, Rfc8949.encode(arr));
    }

    @Test
    void nanCanonicalPayload() {
        LogicalValue v = new LogicalValue.Flt("auto", "NaN");
        assertArrayEquals(Util.hexDecode("f97e00"), Rfc8949.encode(v));
    }

    @Test
    void nanCanonicalPayloadUnderExplicitWidthForcing() {
        for (String width : List.of("f16", "f32", "f64")) {
            LogicalValue v = new LogicalValue.Flt(width, "NaN");
            assertArrayEquals(Util.hexDecode("f97e00"), Rfc8949.encode(v));
        }
    }

    @Test
    void negativeZeroPreservedDistinctFromZero() {
        LogicalValue neg = new LogicalValue.Flt("auto", "-0.0");
        LogicalValue pos = new LogicalValue.Flt("auto", "0.0");
        byte[] negBytes = Rfc8949.encode(neg);
        byte[] posBytes = Rfc8949.encode(pos);
        assertArrayEquals(Util.hexDecode("f98000"), negBytes);
        assertArrayEquals(Util.hexDecode("f90000"), posBytes);
        assertFalse(java.util.Arrays.equals(negBytes, posBytes));
    }

    @Test
    void shortestFloatFormF16Exact() {
        LogicalValue v = new LogicalValue.Flt("auto", "2.5");
        assertArrayEquals(Util.hexDecode("f94100"), Rfc8949.encode(v));
    }

    @Test
    void explicitFloatWidthForcesEncoding() {
        LogicalValue v = new LogicalValue.Flt("f64", "2.5");
        assertArrayEquals(Util.hexDecode("fb4004000000000000"), Rfc8949.encode(v));
    }

    @Test
    void explicitFloatWidthRaisesOnPrecisionLoss() {
        LogicalValue v = new LogicalValue.Flt("f16", "0.1");
        assertThrows(Rfc8949.EncodeException.class, () -> Rfc8949.encode(v));
    }

    @Test
    void mapKeysSortedByEncodedBytes() {
        LogicalValue value = new LogicalValue.Map(List.of(
            java.util.Map.entry(new LogicalValue.Int("9"), new LogicalValue.Int("1")),
            java.util.Map.entry(new LogicalValue.Int("10"), new LogicalValue.Int("2"))));
        assertArrayEquals(Util.hexDecode("a209010a02"), Rfc8949.encode(value));
    }

    @Test
    void mapKeysPureBytewiseNoLengthPresort() {
        // The map-key sort trap: -24 (1 byte, 0x37) must sort AFTER 1000 (3
        // bytes, 0x1903e8) because the comparison is purely bytewise on the
        // encoded key -- a length-first presort (RFC 7049's older rule) would
        // get this backwards.
        LogicalValue value = new LogicalValue.Map(List.of(
            java.util.Map.entry(new LogicalValue.Int("-24"), new LogicalValue.Int("1")),
            java.util.Map.entry(new LogicalValue.Int("1000"), new LogicalValue.Int("2"))));
        assertArrayEquals(Util.hexDecode("a21903e8023701"), Rfc8949.encode(value));
    }

    @Test
    void bignumAt2Pow64UsesTag2() {
        LogicalValue value = new LogicalValue.Bignum("positive", "18446744073709551616");
        assertArrayEquals(Util.hexDecode("c249010000000000000000"), Rfc8949.encode(value));
    }

    @Test
    void negativeBignumOffsetByOne() {
        LogicalValue value = new LogicalValue.Bignum("negative", "18446744073709551616");
        assertArrayEquals(Util.hexDecode("c348ffffffffffffffff"), Rfc8949.encode(value));
    }

    @Test
    void bignumBelow2Pow64IsRejected() {
        LogicalValue positive = new LogicalValue.Bignum("positive", "5");
        assertThrows(Rfc8949.EncodeException.class, () -> Rfc8949.encode(positive));

        LogicalValue negative = new LogicalValue.Bignum("negative", "9223372036854775808");
        assertThrows(Rfc8949.EncodeException.class, () -> Rfc8949.encode(negative));
    }

    @Test
    void tagWrapsInnerValue() {
        LogicalValue value = new LogicalValue.Tag(BigInteger.valueOf(100), new LogicalValue.Int("5"));
        assertArrayEquals(new byte[] { (byte) 0xd8, 0x64, 0x05 }, Rfc8949.encode(value));
    }

    @Test
    void tagAtU64MaxDoesNotTruncate() {
        LogicalValue value = new LogicalValue.Tag(new BigInteger("18446744073709551615"), new LogicalValue.Int("5"));
        assertArrayEquals(Util.hexDecode("dbffffffffffffffff05"), Rfc8949.encode(value));
    }

    @Test
    void mapDuplicateKeysAreNotDeduped() {
        // rfc8949 (P4/P9) has no key-collision dedup logic, unlike dcbor --
        // both entries for a literal duplicate key must survive to the output.
        LogicalValue value = new LogicalValue.Map(List.of(
            java.util.Map.entry(new LogicalValue.Int("5"), new LogicalValue.Int("1")),
            java.util.Map.entry(new LogicalValue.Int("5"), new LogicalValue.Int("2"))));
        assertArrayEquals(Util.hexDecode("a205010502"), Rfc8949.encode(value));
    }
}
