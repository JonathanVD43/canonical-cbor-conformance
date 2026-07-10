import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class UtilTest {
    @Test
    fun roundTrips() {
        assertEquals("deadbeef", hexEncode(byteArrayOf(0xde.toByte(), 0xad.toByte(), 0xbe.toByte(), 0xef.toByte())))
        assertEquals(
            listOf(0xde, 0xad, 0xbe, 0xef).map { it.toByte() },
            hexDecode("deadbeef").toList(),
        )
    }

    @Test
    fun rejectsOddLength() {
        assertFailsWith<IllegalArgumentException> { hexDecode("abc") }
    }

    @Test
    fun rejectsNonAsciiWithoutCrashing() {
        assertFailsWith<IllegalArgumentException> { hexDecode("café") }
    }
}
