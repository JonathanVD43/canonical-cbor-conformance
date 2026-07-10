import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class DecodeTest {
    private fun accept(input: ByteArray, profile: Profile): ByteArray {
        val verdict = decodeStrict(input, profile)
        assertTrue(verdict is Verdict.Accept, "expected Accept, got $verdict")
        return (verdict as Verdict.Accept).bytes
    }

    private fun reject(input: ByteArray, profile: Profile): String {
        val verdict = decodeStrict(input, profile)
        assertTrue(verdict is Verdict.Reject, "expected Reject, got $verdict")
        return (verdict as Verdict.Reject).reason
    }

    @Test
    fun acceptsCanonicalIntAndRoundTrips() {
        val bytes = hexDecode("05")
        assertEquals(bytes.toList(), accept(bytes, Profile.RFC8949).toList())
        assertEquals(bytes.toList(), accept(bytes, Profile.DCBOR).toList())
    }

    @Test
    fun acceptsCanonicalMapAndArray() {
        val arr = hexDecode("820102")
        assertEquals(arr.toList(), accept(arr, Profile.RFC8949).toList())

        val map = hexDecode("a10102")
        assertEquals(map.toList(), accept(map, Profile.RFC8949).toList())
    }

    @Test
    fun nonShortestInt() {
        // 5 encoded with a redundant 1-byte-argument form instead of the direct form.
        val bytes = hexDecode("1805")
        assertEquals("NON_SHORTEST_INT", reject(bytes, Profile.RFC8949))
    }

    @Test
    fun unsortedMapKeys() {
        // Keys 10 then 9, in descending raw-byte order.
        val bytes = hexDecode("a20a010902")
        assertEquals("UNSORTED_MAP_KEYS", reject(bytes, Profile.RFC8949))
    }

    @Test
    fun indefiniteLength() {
        val bytes = hexDecode("9fff")
        assertEquals("INDEFINITE_LENGTH", reject(bytes, Profile.RFC8949))
    }

    @Test
    fun duplicateKey() {
        val bytes = hexDecode("a201010102")
        assertEquals("DUPLICATE_KEY", reject(bytes, Profile.RFC8949))
    }

    @Test
    fun nonShortestFloat() {
        // 2.5 round-trips exactly through f16, so an f32 encoding is non-shortest.
        val bytes = hexDecode("fa40200000")
        assertEquals("NON_SHORTEST_FLOAT", reject(bytes, Profile.RFC8949))
        assertEquals("NON_SHORTEST_FLOAT", reject(bytes, Profile.DCBOR))
    }

    @Test
    fun nanPayloadVariant() {
        val bytes = hexDecode("fa7fc00000")
        assertEquals("NAN_PAYLOAD_VARIANT", reject(bytes, Profile.RFC8949))
    }

    @Test
    fun trailingBytes() {
        val bytes = hexDecode("05ff")
        assertEquals("TRAILING_BYTES", reject(bytes, Profile.RFC8949))
    }

    @Test
    fun multipleTopLevelItems() {
        val bytes = hexDecode("0506")
        assertEquals("MULTIPLE_TOP_LEVEL_ITEMS", reject(bytes, Profile.RFC8949))
    }

    @Test
    fun unknownTag() {
        val bytes = hexDecode("d86405")
        assertEquals("UNKNOWN_TAG", reject(bytes, Profile.RFC8949))
    }

    @Test
    fun nonNfcStringDcborOnly() {
        // "cafe" + combining acute accent (U+0301), not normalized to NFC.
        val bytes = hexDecode("6663616665cc81")
        assertEquals("NON_NFC_STRING", reject(bytes, Profile.DCBOR))
        assertEquals(bytes.toList(), accept(bytes, Profile.RFC8949).toList())
    }

    @Test
    fun unreducedNumericDcborOnly() {
        // 2.0 encoded at its shortest float width (f16) - still must be a plain
        // int under dcbor's D5/D7 rules.
        val bytes = hexDecode("f94000")
        assertEquals("UNREDUCED_NUMERIC", reject(bytes, Profile.DCBOR))
        assertEquals(bytes.toList(), accept(bytes, Profile.RFC8949).toList())
    }
}
