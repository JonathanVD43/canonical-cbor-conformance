package adapter;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class DecodeTest {
    private static byte[] accept(byte[] input, Decode.Profile profile) {
        Decode.Verdict verdict = Decode.decodeStrict(input, profile);
        assertTrue(verdict instanceof Decode.Verdict.Accept, "expected Accept, got " + verdict);
        return ((Decode.Verdict.Accept) verdict).bytes();
    }

    private static String reject(byte[] input, Decode.Profile profile) {
        Decode.Verdict verdict = Decode.decodeStrict(input, profile);
        assertTrue(verdict instanceof Decode.Verdict.Reject, "expected Reject, got " + verdict);
        return ((Decode.Verdict.Reject) verdict).reason();
    }

    @Test
    void acceptsCanonicalIntAndRoundTrips() {
        byte[] bytes = Util.hexDecode("05");
        assertArrayEquals(bytes, accept(bytes, Decode.Profile.RFC8949));
        assertArrayEquals(bytes, accept(bytes, Decode.Profile.DCBOR));
    }

    @Test
    void acceptsCanonicalMapAndArray() {
        byte[] arr = Util.hexDecode("820102");
        assertArrayEquals(arr, accept(arr, Decode.Profile.RFC8949));

        byte[] map = Util.hexDecode("a10102");
        assertArrayEquals(map, accept(map, Decode.Profile.RFC8949));
    }

    @Test
    void nonShortestInt() {
        // 5 encoded with a redundant 1-byte-argument form instead of the direct form.
        byte[] bytes = Util.hexDecode("1805");
        assertEquals("NON_SHORTEST_INT", reject(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void unsortedMapKeys() {
        byte[] bytes = Util.hexDecode("a20a010902");
        assertEquals("UNSORTED_MAP_KEYS", reject(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void indefiniteLength() {
        byte[] bytes = Util.hexDecode("9fff");
        assertEquals("INDEFINITE_LENGTH", reject(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void duplicateKey() {
        byte[] bytes = Util.hexDecode("a201010102");
        assertEquals("DUPLICATE_KEY", reject(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void nonShortestFloat() {
        // 2.5 round-trips exactly through f16, so an f32 encoding is non-shortest.
        byte[] bytes = Util.hexDecode("fa40200000");
        assertEquals("NON_SHORTEST_FLOAT", reject(bytes, Decode.Profile.RFC8949));
        assertEquals("NON_SHORTEST_FLOAT", reject(bytes, Decode.Profile.DCBOR));
    }

    @Test
    void nanPayloadVariant() {
        byte[] bytes = Util.hexDecode("fa7fc00000");
        assertEquals("NAN_PAYLOAD_VARIANT", reject(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void trailingBytes() {
        byte[] bytes = Util.hexDecode("05ff");
        assertEquals("TRAILING_BYTES", reject(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void multipleTopLevelItems() {
        byte[] bytes = Util.hexDecode("0506");
        assertEquals("MULTIPLE_TOP_LEVEL_ITEMS", reject(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void unknownTag() {
        byte[] bytes = Util.hexDecode("d86405");
        assertEquals("UNKNOWN_TAG", reject(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void nonNfcStringDcborOnly() {
        // "cafe" + combining acute accent (U+0301), not normalized to NFC.
        byte[] bytes = Util.hexDecode("6663616665cc81");
        assertEquals("NON_NFC_STRING", reject(bytes, Decode.Profile.DCBOR));
        assertArrayEquals(bytes, accept(bytes, Decode.Profile.RFC8949));
    }

    @Test
    void unreducedNumericDcborOnly() {
        // 2.0 encoded at its shortest float width (f16) -- still must be a
        // plain int under dcbor's D5/D7 rules.
        byte[] bytes = Util.hexDecode("f94000");
        assertEquals("UNREDUCED_NUMERIC", reject(bytes, Decode.Profile.DCBOR));
        assertArrayEquals(bytes, accept(bytes, Decode.Profile.RFC8949));
    }
}
