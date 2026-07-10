import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class Float16Test {
    private fun bits(hex: String): Int = hex.toInt(16)

    @Test
    fun zeroAndNegativeZero() {
        assertEquals(bits("0000"), doubleToF16Bits(0.0))
        assertEquals(bits("8000"), doubleToF16Bits(-0.0))
        assertEquals(0.0, f16BitsToDouble(bits("0000")))
        assertEquals(
            java.lang.Double.doubleToRawLongBits(-0.0),
            java.lang.Double.doubleToRawLongBits(f16BitsToDouble(bits("8000"))),
        )
    }

    @Test
    fun canonicalNanPayload() {
        // 0xf97e00's payload: sign 0, exp 0x1f, mantissa 0x200.
        assertEquals(bits("7e00"), doubleToF16Bits(Double.NaN))
    }

    @Test
    fun infinity() {
        assertEquals(bits("7c00"), doubleToF16Bits(Double.POSITIVE_INFINITY))
        assertEquals(bits("fc00"), doubleToF16Bits(Double.NEGATIVE_INFINITY))
        assertEquals(Double.POSITIVE_INFINITY, f16BitsToDouble(bits("7c00")))
        assertEquals(Double.NEGATIVE_INFINITY, f16BitsToDouble(bits("fc00")))
    }

    @Test
    fun exactlyRepresentableValueRoundTrips() {
        // 2.5 = 0xf94100 payload 0x4100.
        val b = tryF16(2.5)
        assertEquals("4100", b!!.joinToString("") { "%02x".format(it) })
    }

    @Test
    fun nonExactValueRejected() {
        // 0.1 has no exact f16 representation.
        assertNull(tryF16(0.1))
    }

    @Test
    fun smallestNormalAndSubnormalBoundary() {
        // Smallest positive normal half: 2^-14 = 0x0400.
        val smallestNormal = Math.pow(2.0, -14.0)
        assertEquals(bits("0400"), doubleToF16Bits(smallestNormal))
        // Smallest positive subnormal half: 2^-24 = 0x0001.
        val smallestSubnormal = Math.pow(2.0, -24.0)
        assertEquals(bits("0001"), doubleToF16Bits(smallestSubnormal))
    }

    @Test
    fun maxFiniteHalfRoundTrips() {
        // Max finite half value: 65504.0 = 0x7bff.
        assertEquals(bits("7bff"), doubleToF16Bits(65504.0))
        assertEquals(65504.0, f16BitsToDouble(bits("7bff")))
    }

    @Test
    fun overflowRoundsToInfinity() {
        // 65520 rounds up past the max finite half value -> +infinity.
        assertEquals(bits("7c00"), doubleToF16Bits(65520.0))
    }

    @Test
    fun f32RoundTrip() {
        val b = tryF32(2.5)
        assertEquals("40200000", b!!.joinToString("") { "%02x".format(it) })
        assertNull(tryF32(0.1))
    }
}
