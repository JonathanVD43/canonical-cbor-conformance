import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class DcborTest {
    private fun float(value: String) = LogicalValue.Float("auto", value)

    @Test
    fun d5FloatWholeBecomesInt() {
        assertEquals(hexDecode("02").toList(), encodeDcbor(float("2.0")).toList())
    }

    @Test
    fun d5FloatFractionStaysFloat() {
        assertEquals(hexDecode("f94100").toList(), encodeDcbor(float("2.5")).toList())
    }

    @Test
    fun d6NanCanonicalPayload() {
        assertEquals(hexDecode("f97e00").toList(), encodeDcbor(float("NaN")).toList())
    }

    @Test
    fun d7ZeroAsInt() {
        assertEquals(hexDecode("00").toList(), encodeDcbor(LogicalValue.Int("0")).toList())
    }

    @Test
    fun d7ZeroAsPosFloatUnifiesWithInt() {
        assertEquals(hexDecode("00").toList(), encodeDcbor(float("0.0")).toList())
    }

    @Test
    fun d7ZeroAsNegFloatUnifiesWithInt() {
        // Opposite of rfc8949's P7: -0.0 must also collapse to plain int 0.
        assertEquals(hexDecode("00").toList(), encodeDcbor(float("-0.0")).toList())
    }

    @Test
    fun d8NfcCombiningAccentNormalizes() {
        val v = LogicalValue.Text("café")
        assertEquals(hexDecode("65636166c3a9").toList(), encodeDcbor(v).toList())
    }

    @Test
    fun bignumBelowNativeRangeIsRejected() {
        val value = LogicalValue.Bignum("positive", "5")
        assertFailsWith<EncodeException> { encodeDcbor(value) }
    }

    @Test
    fun bignumAt2Pow64UsesTag2() {
        val value = LogicalValue.Bignum("positive", "18446744073709551616")
        assertEquals(hexDecode("c249010000000000000000").toList(), encodeDcbor(value).toList())
    }

    @Test
    fun tagWrapsInnerValue() {
        val value = LogicalValue.Tag(100uL, LogicalValue.Int("5"))
        assertEquals(listOf(0xd8, 0x64, 0x05).map { it.toByte() }, encodeDcbor(value).toList())
    }

    @Test
    fun reservedBignumTagViaGenericTagArmIsARawPassthrough() {
        val value = LogicalValue.Tag(2uL, LogicalValue.Bytes("01"))
        assertEquals(listOf(0xc2, 0x41, 0x01).map { it.toByte() }, encodeDcbor(value).toList())
    }

    @Test
    fun explicitWidthIsIgnoredWholeFloatStillReduces() {
        val v = LogicalValue.Float("f64", "2.0")
        assertEquals(hexDecode("02").toList(), encodeDcbor(v).toList())
    }

    @Test
    fun explicitWidthIsIgnoredNanStillCanonical() {
        val v = LogicalValue.Float("f32", "NaN")
        assertEquals(hexDecode("f97e00").toList(), encodeDcbor(v).toList())
    }

    @Test
    fun tagAtULongMaxDoesNotTruncate() {
        val value = LogicalValue.Tag(ULong.MAX_VALUE, LogicalValue.Int("5"))
        assertEquals(hexDecode("dbffffffffffffffff05").toList(), encodeDcbor(value).toList())
    }

    @Test
    fun mapKeyCollisionDedupesLastWriteWins() {
        // D7: 0, 0.0, and -0.0 all canonicalize to encoded key 0x00, so the
        // reference dcbor crate's Map::insert collapses these three logical
        // entries to one, keeping only the last value written (3). Mirrors
        // adapters/rust/src/dcbor.rs's Map::insert semantics.
        val value = LogicalValue.Map(
            listOf(
                LogicalValue.Int("0") to LogicalValue.Int("1"),
                float("0.0") to LogicalValue.Int("2"),
                float("-0.0") to LogicalValue.Int("3"),
            )
        )
        assertEquals(hexDecode("a10003").toList(), encodeDcbor(value).toList())
    }

    @Test
    fun mapKeyCollisionViaNfcDedupesLastWriteWins() {
        // D8: "café" (precomposed) and "café" (NFC-decomposed) both
        // normalize to the identical encoded key bytes, so they must also
        // collapse to a single entry, last write wins.
        val decomposed = "café" // "e" + combining acute accent (U+0301)
        val precomposed = "café" // single precomposed char (U+00E9)
        val value = LogicalValue.Map(
            listOf(
                LogicalValue.Text(decomposed) to LogicalValue.Int("1"),
                LogicalValue.Text(precomposed) to LogicalValue.Int("2"),
            )
        )
        assertEquals(hexDecode("a165636166c3a902").toList(), encodeDcbor(value).toList())
    }

    @Test
    fun arrayAndMapRoundTrip() {
        val arr = LogicalValue.Array(listOf(LogicalValue.Int("1"), LogicalValue.Int("2")))
        assertEquals(listOf(0x82, 0x01, 0x02).map { it.toByte() }, encodeDcbor(arr).toList())

        val map = LogicalValue.Map(listOf(LogicalValue.Text("a") to LogicalValue.Int("1")))
        assertEquals(hexDecode("a1616101").toList(), encodeDcbor(map).toList())
    }
}
