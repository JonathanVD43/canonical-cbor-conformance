#!/usr/bin/env node
/**
 * CLI entry point + batch stdin/stdout protocol (design spec §7).
 * Faithful port of adapters/rust/src/main.rs (cross-checked against
 * adapters/kotlin/src/main/kotlin/Main.kt).
 */

import { createInterface } from "node:readline";

import { hexEncode, hexDecode } from "./util.ts";
import { parseLogicalValueLine } from "./logicalValue.ts";
import { encodeRfc8949, EncodeError } from "./rfc8949.ts";
import { encodeDcbor } from "./dcbor.ts";
import { decodeStrict } from "./decode.ts";
import type { Profile } from "./decode.ts";
import type { LogicalValue } from "./logicalValue.ts";

async function forEachLine(fn: (line: string) => void): Promise<void> {
  const rl = createInterface({ input: process.stdin, crlfDelay: Infinity });
  for await (const line of rl) {
    const trimmed = line.trim();
    if (trimmed.length === 0) continue;
    fn(trimmed);
  }
}

function parseProfile(args: string[]): string | undefined {
  const i = args.indexOf("--profile");
  if (i === -1) return undefined;
  return args[i + 1];
}

async function runEncode(profileArg: string | undefined): Promise<number> {
  let encoder: (value: LogicalValue) => Uint8Array;
  switch (profileArg) {
    case "rfc8949":
      encoder = encodeRfc8949;
      break;
    case "dcbor":
      encoder = encodeDcbor;
      break;
    case undefined:
      console.error("--profile is required");
      return 2;
    default:
      console.error(`unsupported profile: ${profileArg}`);
      return 3;
  }

  let hadError = false;
  await forEachLine((line) => {
    let logical;
    try {
      logical = parseLogicalValueLine(line);
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      console.error(`malformed input line: ${message}`);
      hadError = true;
      console.log();
      return;
    }
    try {
      const bytes = encoder(logical);
      console.log(hexEncode(bytes));
    } catch (e) {
      if (e instanceof EncodeError) {
        console.error(`encode rejected: ${e.message}`);
        hadError = true;
        console.log();
        return;
      }
      throw e;
    }
  });

  return hadError ? 1 : 0;
}

async function runDecodeStrict(profileArg: string | undefined): Promise<number> {
  let profile: Profile;
  switch (profileArg) {
    case "rfc8949":
      profile = "rfc8949";
      break;
    case "dcbor":
      profile = "dcbor";
      break;
    case undefined:
      console.error("--profile is required");
      return 2;
    default:
      console.error(`unsupported profile: ${profileArg}`);
      return 3;
  }

  let hadError = false;
  await forEachLine((line) => {
    let bytes: Uint8Array;
    try {
      bytes = hexDecode(line);
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      console.error(`malformed input line: ${message}`);
      hadError = true;
      console.log();
      return;
    }
    try {
      const verdict = decodeStrict(bytes, profile);
      if (verdict.kind === "accept") {
        console.log(`ACCEPT ${hexEncode(verdict.bytes)}`);
      } else {
        console.log(`REJECT ${verdict.reason}`);
      }
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      console.error(`decode-strict internal error: ${message}`);
      hadError = true;
      console.log();
      return;
    }
  });

  return hadError ? 1 : 0;
}

async function main(): Promise<number> {
  const args = process.argv.slice(2);
  if (args.length < 1) {
    console.error("usage: adapter <mode> --profile <profile>");
    return 2;
  }
  const profileArg = parseProfile(args);
  switch (args[0]) {
    case "encode":
      return runEncode(profileArg);
    case "decode-strict":
      return runDecodeStrict(profileArg);
    default:
      console.error(`unknown mode: ${args[0]}`);
      return 2;
  }
}

main().then((code) => {
  process.exitCode = code;
});
