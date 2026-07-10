import java.io.IOException
import kotlin.system.exitProcess
import org.json.JSONException
import org.json.JSONTokener

// PrintStream never throws on write failure; it sets an internal error flag
// instead. Check it after every write to detect the failure that main.rs
// catches via writeln!'s Result, and exit 2 the same way it does.
private fun checkStdoutOrExit(out: java.io.PrintStream) {
    if (out.checkError()) {
        System.err.println("internal adapter error: failed to write stdout")
        exitProcess(2)
    }
}

fun main(args: Array<String>) {
    if (args.size < 1) {
        System.err.println("usage: adapter <mode> --profile <profile>")
        exitProcess(2)
    }
    val profileIndex = args.indexOf("--profile")
    val profile = if (profileIndex >= 0 && profileIndex + 1 < args.size) args[profileIndex + 1] else null

    val exitCode = when (args[0]) {
        "encode" -> runEncode(profile)
        "decode-strict" -> runDecodeStrict(profile)
        else -> {
            System.err.println("unknown mode: ${args[0]}")
            2
        }
    }
    exitProcess(exitCode)
}

private fun runEncode(profile: String?): Int {
    val encoder: (LogicalValue) -> ByteArray = when (profile) {
        "rfc8949" -> ::encodeRfc8949
        "dcbor" -> ::encodeDcbor
        null -> {
            System.err.println("--profile is required")
            return 2
        }
        else -> {
            System.err.println("unsupported profile: $profile")
            return 3
        }
    }

    var hadError = false
    val out = System.out
    try {
        System.`in`.bufferedReader().forEachLine { line ->
            val trimmed = line.trim()
            if (trimmed.isEmpty()) return@forEachLine

            val json = try {
                JSONTokener(trimmed).nextValue()
            } catch (e: JSONException) {
                System.err.println("malformed input line: ${e.message}")
                hadError = true
                out.println()
                checkStdoutOrExit(out)
                return@forEachLine
            }

            val logical = try {
                parseLogicalValue(json)
            } catch (e: ParseException) {
                System.err.println("malformed input line: ${e.message}")
                hadError = true
                out.println()
                checkStdoutOrExit(out)
                return@forEachLine
            }

            try {
                out.println(hexEncode(encoder(logical)))
                checkStdoutOrExit(out)
            } catch (e: EncodeException) {
                System.err.println("encode rejected: ${e.message}")
                hadError = true
                out.println()
                checkStdoutOrExit(out)
            }
        }
    } catch (e: IOException) {
        System.err.println("internal adapter error: failed to read stdin: ${e.message}")
        exitProcess(2)
    }
    return if (hadError) 1 else 0
}

private fun runDecodeStrict(profile: String?): Int {
    val decodeProfile = when (profile) {
        "rfc8949" -> Profile.RFC8949
        "dcbor" -> Profile.DCBOR
        null -> {
            System.err.println("--profile is required")
            return 2
        }
        else -> {
            System.err.println("unsupported profile: $profile")
            return 3
        }
    }

    var hadError = false
    val out = System.out
    try {
        System.`in`.bufferedReader().forEachLine { line ->
            val trimmed = line.trim()
            if (trimmed.isEmpty()) return@forEachLine

            val bytes = try {
                hexDecode(trimmed)
            } catch (e: IllegalArgumentException) {
                System.err.println("malformed input line: ${e.message}")
                hadError = true
                out.println()
                checkStdoutOrExit(out)
                return@forEachLine
            }

            try {
                when (val verdict = decodeStrict(bytes, decodeProfile)) {
                    is Verdict.Accept -> out.println("ACCEPT ${hexEncode(verdict.bytes)}")
                    is Verdict.Reject -> out.println("REJECT ${verdict.reason}")
                }
                checkStdoutOrExit(out)
            } catch (e: DecodeException) {
                System.err.println("decode-strict internal error: ${e.message}")
                hadError = true
                out.println()
                checkStdoutOrExit(out)
            }
        }
    } catch (e: IOException) {
        System.err.println("internal adapter error: failed to read stdin: ${e.message}")
        exitProcess(2)
    }
    return if (hadError) 1 else 0
}
