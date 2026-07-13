package adapter;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.core.JsonProcessingException;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.function.Function;

public final class Main {
    private Main() {}

    // USE_BIG_INTEGER_FOR_INTS: the `tag` field can be up to 2^64-1, past a
    // signed 64-bit intermediate's range -- without this, Jackson would
    // parse large integer literals as a `long` and silently overflow.
    private static final ObjectMapper MAPPER = new ObjectMapper()
        .configure(DeserializationFeature.USE_BIG_INTEGER_FOR_INTS, true);

    public static void main(String[] args) {
        if (args.length < 1) {
            System.err.println("usage: adapter <mode> --profile <profile>");
            System.exit(2);
        }
        int profileIndex = Arrays.asList(args).indexOf("--profile");
        String profile = (profileIndex >= 0 && profileIndex + 1 < args.length) ? args[profileIndex + 1] : null;

        int exitCode;
        switch (args[0]) {
            case "encode":
                exitCode = runEncode(profile);
                break;
            case "decode-strict":
                exitCode = runDecodeStrict(profile);
                break;
            default:
                System.err.println("unknown mode: " + args[0]);
                exitCode = 2;
        }
        System.exit(exitCode);
    }

    // System.out never throws on write failure by default; check its error
    // flag after every write to detect it, mirroring how main.rs surfaces a
    // stdout write failure as exit 2.
    private static void checkStdoutOrExit(PrintStream out) {
        if (out.checkError()) {
            System.err.println("internal adapter error: failed to write stdout");
            System.exit(2);
        }
    }

    private static int runEncode(String profile) {
        Function<LogicalValue, byte[]> encoder;
        if (profile == null) {
            System.err.println("--profile is required");
            return 2;
        }
        switch (profile) {
            case "rfc8949": encoder = Rfc8949::encode; break;
            case "dcbor": encoder = Dcbor::encode; break;
            default:
                System.err.println("unsupported profile: " + profile);
                return 3;
        }

        boolean hadError = false;
        PrintStream out = System.out;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                String trimmed = line.trim();
                if (trimmed.isEmpty()) continue;

                JsonNode json;
                try {
                    json = MAPPER.readTree(trimmed);
                } catch (JsonProcessingException e) {
                    System.err.println("malformed input line: " + e.getMessage());
                    hadError = true;
                    out.println();
                    checkStdoutOrExit(out);
                    continue;
                }

                LogicalValue logical;
                try {
                    logical = LogicalValue.parse(json);
                } catch (LogicalValue.ParseException e) {
                    System.err.println("malformed input line: " + e.getMessage());
                    hadError = true;
                    out.println();
                    checkStdoutOrExit(out);
                    continue;
                }

                try {
                    out.println(Util.hexEncode(encoder.apply(logical)));
                    checkStdoutOrExit(out);
                } catch (Rfc8949.EncodeException e) {
                    System.err.println("encode rejected: " + e.getMessage());
                    hadError = true;
                    out.println();
                    checkStdoutOrExit(out);
                }
            }
        } catch (IOException e) {
            System.err.println("internal adapter error: failed to read stdin: " + e.getMessage());
            System.exit(2);
        }
        return hadError ? 1 : 0;
    }

    private static int runDecodeStrict(String profile) {
        Decode.Profile decodeProfile;
        if (profile == null) {
            System.err.println("--profile is required");
            return 2;
        }
        switch (profile) {
            case "rfc8949": decodeProfile = Decode.Profile.RFC8949; break;
            case "dcbor": decodeProfile = Decode.Profile.DCBOR; break;
            default:
                System.err.println("unsupported profile: " + profile);
                return 3;
        }

        boolean hadError = false;
        PrintStream out = System.out;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                String trimmed = line.trim();
                if (trimmed.isEmpty()) continue;

                byte[] bytes;
                try {
                    bytes = Util.hexDecode(trimmed);
                } catch (IllegalArgumentException e) {
                    System.err.println("malformed input line: " + e.getMessage());
                    hadError = true;
                    out.println();
                    checkStdoutOrExit(out);
                    continue;
                }

                try {
                    Decode.Verdict verdict = Decode.decodeStrict(bytes, decodeProfile);
                    if (verdict instanceof Decode.Verdict.Accept v) {
                        out.println("ACCEPT " + Util.hexEncode(v.bytes()));
                    } else if (verdict instanceof Decode.Verdict.Reject v) {
                        out.println("REJECT " + v.reason());
                    }
                    checkStdoutOrExit(out);
                } catch (Decode.DecodeException e) {
                    System.err.println("decode-strict internal error: " + e.getMessage());
                    hadError = true;
                    out.println();
                    checkStdoutOrExit(out);
                }
            }
        } catch (IOException e) {
            System.err.println("internal adapter error: failed to read stdin: " + e.getMessage());
            System.exit(2);
        }
        return hadError ? 1 : 0;
    }
}
