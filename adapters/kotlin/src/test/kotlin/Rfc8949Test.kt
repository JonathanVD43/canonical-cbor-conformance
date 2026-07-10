import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotEquals

class Rfc8949Test {
    @Test
    fun shortestIntZero() {
        assertEquals(listOf(0x00.toByte()), encodeRfc8949(LogicalValue.Int("0")).toList())
    }

    @Test
    fun shortestIntBoundary23_24() {
        assertEquals(listOf(0x17.toByte()), encodeRfc8949(LogicalValue.Int("23")).toList())
        assertEquals(listOf(0x18.toByte(), 0x18.toByte()), encodeRfc8949(LogicalValue.Int("24")).toList())
    }

    @Test
    fun shortestIntBoundary255_256() {
        assertEquals(listOf(0x18.toByte(), 0xff.toByte()), encodeRfc8949(LogicalValue.Int("255")).toList())
        assertEquals(
            listOf(0x19.toByte(), 0x01.toByte(), 0x00.toByte()),
            encodeRfc8949(LogicalValue.Int("256")).toList(),
        )
    }

    @Test
    fun shortestIntBoundary65535_65536() {
        assertEquals(
            listOf(0x19.toByte(), 0xff.toByte(), 0xff.toByte()),
            encodeRfc8949(LogicalValue.Int("65535")).toList(),
        )
        assertEquals(
            listOf(0x1a.toByte(), 0x00.toByte(), 0x01.toByte(), 0x00.toByte(), 0x00.toByte()),
            encodeRfc8949(LogicalValue.Int("65536")).toList(),
        )
    }

    @Test
    fun shortestIntBoundary4294967295_4294967296() {
        assertEquals(
            listOf(0x1a.toByte(), 0xff.toByte(), 0xff.toByte(), 0xff.toByte(), 0xff.toByte()),
            encodeRfc8949(LogicalValue.Int("4294967295")).toList(),
        )
        assertEquals(
            hexDecode("1b0000000100000000").toList(),
            encodeRfc8949(LogicalValue.Int("4294967296")).toList(),
        )
    }

    @Test
    fun negativeInt() {
        assertEquals(listOf(0x20.toByte()), encodeRfc8949(LogicalValue.Int("-1")).toList())
    }

    @Test
    fun negativeIntMinI64Boundary() {
        assertEquals(
            hexDecode("3b7fffffffffffffff").toList(),
            encodeRfc8949(LogicalValue.Int("-9223372036854775808")).toList(),
        )
    }

    @Test
    fun textAndBytes() {
        assertEquals(listOf(0x61.toByte(), 'a'.code.toByte()), encodeRfc8949(LogicalValue.Text("a")).toList())
        assertEquals(
            listOf(0x44.toByte(), 0xde.toByte(), 0xad.toByte(), 0xbe.toByte(), 0xef.toByte()),
            encodeRfc8949(LogicalValue.Bytes("deadbeef")).toList(),
        )
    }

    @Test
    fun boolAndNull() {
        assertEquals(listOf(0xf5.toByte()), encodeRfc8949(LogicalValue.Bool(true)).toList())
        assertEquals(listOf(0xf4.toByte()), encodeRfc8949(LogicalValue.Bool(false)).toList())
        assertEquals(listOf(0xf6.toByte()), encodeRfc8949(LogicalValue.Null).toList())
    }

    @Test
    fun arrayOfInts() {
        val arr = LogicalValue.Array(listOf(LogicalValue.Int("1"), LogicalValue.Int("2")))
        assertEquals(listOf(0x82, 0x01, 0x02).map { it.toByte() }, encodeRfc8949(arr).toList())
    }

    @Test
    fun nanCanonicalPayload() {
        val v = LogicalValue.Float("auto", "NaN")
        assertEquals(hexDecode("f97e00").toList(), encodeRfc8949(v).toList())
    }

    @Test
    fun nanCanonicalPayloadUnderExplicitWidthForcing() {
        for (width in listOf("f16", "f32", "f64")) {
            val v = LogicalValue.Float(width, "NaN")
            assertEquals(hexDecode("f97e00").toList(), encodeRfc8949(v).toList())
        }
    }

    @Test
    fun negativeZeroPreservedDistinctFromZero() {
        val neg = LogicalValue.Float("auto", "-0.0")
        val pos = LogicalValue.Float("auto", "0.0")
        val negBytes = encodeRfc8949(neg)
        val posBytes = encodeRfc8949(pos)
        assertEquals(hexDecode("f98000").toList(), negBytes.toList())
        assertEquals(hexDecode("f90000").toList(), posBytes.toList())
        assertNotEquals(negBytes.toList(), posBytes.toList())
    }

    @Test
    fun shortestFloatFormF16Exact() {
        val v = LogicalValue.Float("auto", "2.5")
        assertEquals(hexDecode("f94100").toList(), encodeRfc8949(v).toList())
    }

    @Test
    fun explicitFloatWidthForcesEncoding() {
        val v = LogicalValue.Float("f64", "2.5")
        assertEquals(hexDecode("fb4004000000000000").toList(), encodeRfc8949(v).toList())
    }

    @Test
    fun explicitFloatWidthRaisesOnPrecisionLoss() {
        val v = LogicalValue.Float("f16", "0.1")
        assertFailsWith<EncodeException> { encodeRfc8949(v) }
    }

    @Test
    fun mapKeysSortedByEncodedBytes() {
        val value = LogicalValue.Map(
            listOf(
                LogicalValue.Int("9") to LogicalValue.Int("1"),
                LogicalValue.Int("10") to LogicalValue.Int("2"),
            )
        )
        assertEquals(hexDecode("a209010a02").toList(), encodeRfc8949(value).toList())
    }

    @Test
    fun mapKeysPureBytewiseNoLengthPresort() {
        val value = LogicalValue.Map(
            listOf(
                LogicalValue.Int("-24") to LogicalValue.Int("1"),
                LogicalValue.Int("1000") to LogicalValue.Int("2"),
            )
        )
        assertEquals(hexDecode("a21903e8023701").toList(), encodeRfc8949(value).toList())
    }

    @Test
    fun bignumAt2Pow64UsesTag2() {
        val value = LogicalValue.Bignum("positive", "18446744073709551616")
        assertEquals(hexDecode("c249010000000000000000").toList(), encodeRfc8949(value).toList())
    }

    @Test
    fun negativeBignumOffsetByOne() {
        val value = LogicalValue.Bignum("negative", "18446744073709551616")
        assertEquals(hexDecode("c348ffffffffffffffff").toList(), encodeRfc8949(value).toList())
    }

    @Test
    fun bignumBelow2Pow64IsRejected() {
        val positive = LogicalValue.Bignum("positive", "5")
        assertFailsWith<EncodeException> { encodeRfc8949(positive) }

        val negative = LogicalValue.Bignum("negative", "9223372036854775808")
        assertFailsWith<EncodeException> { encodeRfc8949(negative) }
    }

    @Test
    fun tagWrapsInnerValue() {
        val value = LogicalValue.Tag(100uL, LogicalValue.Int("5"))
        assertEquals(listOf(0xd8, 0x64, 0x05).map { it.toByte() }, encodeRfc8949(value).toList())
    }

    @Test
    fun tagAtULongMaxDoesNotTruncate() {
        // Regression: Tag.tag used to be Long, so BigInteger.valueOf(value.tag)
        // silently wrapped ULong.MAX_VALUE to -1 instead of encoding the true
        // 8-byte unsigned argument 0xffffffffffffffff.
        val value = LogicalValue.Tag(ULong.MAX_VALUE, LogicalValue.Int("5"))
        assertEquals(hexDecode("dbffffffffffffffff05").toList(), encodeRfc8949(value).toList())
    }

    @Test
    fun mapDuplicateKeysAreNotDeduped() {
        // rfc8949 (P4/P9) has no key-collision dedup logic, unlike dcbor
        // (see DcborTest.mapKeyCollisionDedupesLastWriteWins) - both entries
        // for a literal duplicate key must survive to the output.
        val value = LogicalValue.Map(
            listOf(
                LogicalValue.Int("5") to LogicalValue.Int("1"),
                LogicalValue.Int("5") to LogicalValue.Int("2"),
            )
        )
        assertEquals(hexDecode("a205010502").toList(), encodeRfc8949(value).toList())
    }
}
