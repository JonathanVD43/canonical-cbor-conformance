package adapter;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class UtilTest {
    @Test
    void roundTrips() {
        assertEquals("deadbeef", Util.hexEncode(new byte[] { (byte) 0xde, (byte) 0xad, (byte) 0xbe, (byte) 0xef }));
        assertArrayEquals(new byte[] { (byte) 0xde, (byte) 0xad, (byte) 0xbe, (byte) 0xef }, Util.hexDecode("deadbeef"));
    }

    @Test
    void rejectsOddLength() {
        assertThrows(IllegalArgumentException.class, () -> Util.hexDecode("abc"));
    }

    @Test
    void rejectsNonAsciiWithoutCrashing() {
        assertThrows(IllegalArgumentException.class, () -> Util.hexDecode("café"));
    }
}
