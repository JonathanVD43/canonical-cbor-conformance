package adapter;

/** Hex helpers shared by encode and decode-strict. */
public final class Util {
    private Util() {}

    public static String hexEncode(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) {
            sb.append(String.format("%02x", b & 0xff));
        }
        return sb.toString();
    }

    public static byte[] hexDecode(String s) {
        if (s.length() % 2 != 0) {
            throw new IllegalArgumentException("hex string has odd length: \"" + s + "\"");
        }
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) > 127) {
                throw new IllegalArgumentException("hex string contains non-ASCII characters: \"" + s + "\"");
            }
        }
        byte[] out = new byte[s.length() / 2];
        for (int i = 0; i < out.length; i++) {
            String byteStr = s.substring(i * 2, i * 2 + 2);
            int value;
            try {
                value = Integer.parseInt(byteStr, 16);
            } catch (NumberFormatException e) {
                throw new IllegalArgumentException("invalid hex byte \"" + byteStr + "\"");
            }
            out[i] = (byte) value;
        }
        return out;
    }
}
