package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"
)

// CLI entry point + batch stdin/stdout protocol (CONTRIBUTING.md's adapter
// contract), ported from adapters/kotlin/src/main/kotlin/Main.kt (itself
// cross-checked against adapters/rust and adapters/typescript).

func main() {
	os.Exit(run(os.Args[1:]))
}

func run(args []string) int {
	if len(args) < 1 {
		fmt.Fprintln(os.Stderr, "usage: adapter <mode> --profile <profile>")
		return 2
	}
	profile := parseProfileArg(args)

	switch args[0] {
	case "encode":
		return runEncode(profile)
	case "decode-strict":
		return runDecodeStrict(profile)
	default:
		fmt.Fprintf(os.Stderr, "unknown mode: %s\n", args[0])
		return 2
	}
}

func parseProfileArg(args []string) *string {
	for i, a := range args {
		if a == "--profile" && i+1 < len(args) {
			return &args[i+1]
		}
	}
	return nil
}

// forEachLine reads stdin line by line, skipping blank lines, calling fn for
// each non-blank trimmed line. Returns an error only on a genuine stdin read
// failure (mirrors the reference adapters' IOException handling -> exit 2).
func forEachLine(fn func(line string)) error {
	scanner := bufio.NewScanner(os.Stdin)
	scanner.Buffer(make([]byte, 0, 64*1024), 16*1024*1024)
	for scanner.Scan() {
		trimmed := strings.TrimSpace(scanner.Text())
		if trimmed == "" {
			continue
		}
		fn(trimmed)
	}
	return scanner.Err()
}

// stdoutWriter wraps a bufio.Writer and tracks the first write error, the
// same way the Kotlin adapter checks PrintStream.checkError() after every
// write to detect a stdout failure and exit 2.
type stdoutWriter struct {
	w   *bufio.Writer
	err error
}

func newStdoutWriter() *stdoutWriter {
	return &stdoutWriter{w: bufio.NewWriter(os.Stdout)}
}

func (s *stdoutWriter) println(line string) {
	if s.err != nil {
		return
	}
	if _, err := s.w.WriteString(line); err != nil {
		s.err = err
		return
	}
	if err := s.w.WriteByte('\n'); err != nil {
		s.err = err
		return
	}
	if err := s.w.Flush(); err != nil {
		s.err = err
	}
}

func runEncode(profile *string) int {
	var encoder func(LogicalValue) ([]byte, error)
	switch {
	case profile == nil:
		fmt.Fprintln(os.Stderr, "--profile is required")
		return 2
	case *profile == "rfc8949":
		encoder = encodeRfc8949
	case *profile == "dcbor":
		encoder = encodeDcbor
	default:
		fmt.Fprintf(os.Stderr, "unsupported profile: %s\n", *profile)
		return 3
	}

	hadError := false
	out := newStdoutWriter()
	err := forEachLine(func(line string) {
		logical, err := parseLogicalValueLine(line)
		if err != nil {
			fmt.Fprintf(os.Stderr, "malformed input line: %v\n", err)
			hadError = true
			out.println("")
			return
		}
		bytes, err := encoder(logical)
		if err != nil {
			fmt.Fprintf(os.Stderr, "encode rejected: %v\n", err)
			hadError = true
			out.println("")
			return
		}
		out.println(hexEncode(bytes))
	})
	if out.err != nil {
		fmt.Fprintln(os.Stderr, "internal adapter error: failed to write stdout")
		return 2
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "internal adapter error: failed to read stdin: %v\n", err)
		return 2
	}
	if hadError {
		return 1
	}
	return 0
}

func runDecodeStrict(profile *string) int {
	var decodeProfile Profile
	switch {
	case profile == nil:
		fmt.Fprintln(os.Stderr, "--profile is required")
		return 2
	case *profile == "rfc8949":
		decodeProfile = ProfileRFC8949
	case *profile == "dcbor":
		decodeProfile = ProfileDCBOR
	default:
		fmt.Fprintf(os.Stderr, "unsupported profile: %s\n", *profile)
		return 3
	}

	hadError := false
	out := newStdoutWriter()
	err := forEachLine(func(line string) {
		bytes, err := hexDecodeStrict(line)
		if err != nil {
			fmt.Fprintf(os.Stderr, "malformed input line: %v\n", err)
			hadError = true
			out.println("")
			return
		}
		verdict, err := decodeStrict(bytes, decodeProfile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "decode-strict internal error: %v\n", err)
			hadError = true
			out.println("")
			return
		}
		if verdict.Accept {
			out.println("ACCEPT " + hexEncode(verdict.Bytes))
		} else {
			out.println("REJECT " + verdict.Reason)
		}
	})
	if out.err != nil {
		fmt.Fprintln(os.Stderr, "internal adapter error: failed to write stdout")
		return 2
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "internal adapter error: failed to read stdin: %v\n", err)
		return 2
	}
	if hadError {
		return 1
	}
	return 0
}
