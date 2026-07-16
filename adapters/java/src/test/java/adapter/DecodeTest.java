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
    void nonCanonicalBignum() {
        // (a) magnitude fits native range: tag 2 wrapping magnitude 1.
        assertEquals("NON_CANONICAL_BIGNUM", reject(Util.hexDecode("c24101"), Decode.Profile.RFC8949));
        assertEquals("NON_CANONICAL_BIGNUM", reject(Util.hexDecode("c24101"), Decode.Profile.DCBOR));
        // (a) exact boundary: 2^64-1 (8-byte all-ones).
        assertEquals("NON_CANONICAL_BIGNUM", reject(Util.hexDecode("c248ffffffffffffffff"), Decode.Profile.RFC8949));
        // (b) non-minimal length: 2^64 with a leading zero byte.
        assertEquals("NON_CANONICAL_BIGNUM", reject(Util.hexDecode("c24a00010000000000000000"), Decode.Profile.RFC8949));
        // tag 3 negative equivalents.
        assertEquals("NON_CANONICAL_BIGNUM", reject(Util.hexDecode("c34101"), Decode.Profile.DCBOR));
        assertEquals("NON_CANONICAL_BIGNUM", reject(Util.hexDecode("c34a00010000000000000000"), Decode.Profile.RFC8949));
        // Genuinely canonical bignums (magnitude >= 2^64, minimal) still ACCEPT.
        byte[] ok = Util.hexDecode("c249010000000000000000");
        assertArrayEquals(ok, accept(ok, Decode.Profile.RFC8949));
        byte[] okNeg = Util.hexDecode("c349010000000000000000");
        assertArrayEquals(okNeg, accept(okNeg, Decode.Profile.RFC8949));
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
