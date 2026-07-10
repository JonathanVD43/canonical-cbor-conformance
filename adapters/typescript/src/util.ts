/**
 * Hex encode/decode utilities with strict validation.
 */

/**
 * Encodes a buffer to a lowercase hex string.
 */
export function hexEncode(bytes: Uint8Array | Buffer): string {
  return Buffer.from(bytes).toString("hex");
}

/**
 * Decodes a hex string to a buffer.
 * Throws an Error if the string has odd length or contains non-hex characters.
 */
export function hexDecode(hex: string): Buffer {
  // Reject odd-length strings
  if (hex.length % 2 !== 0) {
    throw new Error(`Invalid hex string: odd length (${hex.length})`);
  }

  // Reject any non-hex characters
  if (!/^[0-9a-f]*$/i.test(hex)) {
    throw new Error(`Invalid hex string: contains non-hex characters`);
  }

  return Buffer.from(hex, "hex");
}
