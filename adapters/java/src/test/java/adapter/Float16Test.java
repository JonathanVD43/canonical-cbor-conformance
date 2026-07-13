package adapter;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class Float16Test {
    private static int bits(String hex) {
        return Integer.parseInt(hex, 16);
    }

    @Test
    void zeroAndNegativeZero() {
        assertEquals(bits("0000"), Float16.doubleToF16Bits(0.0));
        assertEquals(bits("8000"), Float16.doubleToF16Bits(-0.0));
        assertEquals(0.0, Float16.f16BitsToDouble(bits("0000")));
        assertEquals(
            Double.doubleToRawLongBits(-0.0),
            Double.doubleToRawLongBits(Float16.f16BitsToDouble(bits("8000"))));
    }

    @Test
    void canonicalNanPayload() {
        assertEquals(bits("7e00"), Float16.doubleToF16Bits(Double.NaN));
    }

    @Test
    void infinity() {
        assertEquals(bits("7c00"), Float16.doubleToF16Bits(Double.POSITIVE_INFINITY));
        assertEquals(bits("fc00"), Float16.doubleToF16Bits(Double.NEGATIVE_INFINITY));
        assertEquals(Double.POSITIVE_INFINITY, Float16.f16BitsToDouble(bits("7c00")));
        assertEquals(Double.NEGATIVE_INFINITY, Float16.f16BitsToDouble(bits("fc00")));
    }

    @Test
    void exactlyRepresentableValueRoundTrips() {
        byte[] b = Float16.tryF16(2.5);
        assertNotNull(b);
        assertEquals("4100", Util.hexEncode(b));
    }

    @Test
    void nonExactValueRejected() {
        assertNull(Float16.tryF16(0.1));
    }

    @Test
    void smallestNormalAndSubnormalBoundary() {
        double smallestNormal = Math.pow(2.0, -14.0);
        assertEquals(bits("0400"), Float16.doubleToF16Bits(smallestNormal));
        double smallestSubnormal = Math.pow(2.0, -24.0);
        assertEquals(bits("0001"), Float16.doubleToF16Bits(smallestSubnormal));
    }

    @Test
    void maxFiniteHalfRoundTrips() {
        assertEquals(bits("7bff"), Float16.doubleToF16Bits(65504.0));
        assertEquals(65504.0, Float16.f16BitsToDouble(bits("7bff")));
    }

    @Test
    void overflowRoundsToInfinity() {
        assertEquals(bits("7c00"), Float16.doubleToF16Bits(65520.0));
    }

    @Test
    void subnormalExhaustiveRoundTrip() {
        // Regression test for a found-and-fixed bug: f16BitsToDouble's
        // subnormal branch used to start expAdj at -1 instead of 1,
        // undercounting the normalization shift by 2 and silently
        // quartering the magnitude of every subnormal f16 value on decode.
        // No existing vector exercised this (only the encode direction was
        // covered by smallestNormalAndSubnormalBoundary above); caught by
        // an independent QA pass after the same bug was found and fixed in
        // the C and Go adapters first.
        for (int bits = 0; bits <= 0x03ff; bits++) {
            double got = Float16.f16BitsToDouble(bits);
            double want = bits * Math.pow(2.0, -24.0);
            assertEquals(want, got, 0.0, "bits=0x" + Integer.toHexString(bits));
        }
    }

    @Test
    void f32RoundTrip() {
        byte[] b = Float16.tryF32(2.5);
        assertNotNull(b);
        assertEquals("40200000", Util.hexEncode(b));
        assertNull(Float16.tryF32(0.1));
    }
}
