// Copyright (c) YugabyteDB, Inc.

// Command yb-log-parse answers log-file questions for support bundle collection on a DB node.
//
// Invoked by devops/bin/node_utils.sh, which captures stdout with $(...) and falls back to inline
// Python on a non-zero exit. See the logparse package doc for the output contract; in particular
// stdout carries no trailing newline, and `add-days 20260101 0` is the contracted pre-flight probe.
package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/devops/supportbundle/logparse"
)

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	switch os.Args[1] {
	case "match":
		runMatch(os.Args[2:])
	case "match-batch":
		runMatchBatch(os.Args[2:])
	case "normalize-sortable":
		runSingleArg("normalize-sortable", os.Args[2:], logparse.NormalizeDateToSortable)
	case "normalize-compact":
		runSingleArg("normalize-compact", os.Args[2:], logparse.NormalizeDateToCompact)
	case "decode-b64":
		runSingleArg("decode-b64", os.Args[2:], logparse.DecodeBase64String)
	case "add-days":
		runAddDays(os.Args[2:])
	default:
		usage()
		os.Exit(2)
	}
}

func usage() {
	fmt.Fprintf(os.Stderr, `usage:
  %s match --file-name NAME --regex PATTERN [--regex PATTERN...]
  %s match-batch --regex PATTERN [--regex PATTERN...]
  %s normalize-sortable DATE
  %s normalize-compact DATE
  %s decode-b64 ENCODED
  %s add-days YYYYMMDD DELTA
`, os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0])
}

// runSingleArg handles the subcommands that take exactly one positional argument and print one
// value.
func runSingleArg(cmd string, args []string, fn func(string) (string, error)) {
	if len(args) != 1 {
		fmt.Fprintf(os.Stderr, "%s requires exactly one argument\n", cmd)
		os.Exit(2)
	}
	out, err := fn(args[0])
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		os.Exit(1)
	}
	fmt.Print(out)
}

// runAddDays shifts a YYYYMMDD date by a signed number of days. Also the pre-flight probe: a
// delta of 0 must echo the input unchanged.
func runAddDays(args []string) {
	if len(args) != 2 {
		fmt.Fprintln(os.Stderr, "add-days requires YYYYMMDD and DELTA")
		os.Exit(2)
	}
	out, err := logparse.CompactDateAddDays(args[0], args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		os.Exit(1)
	}
	fmt.Print(out)
}

// runMatch classifies a single filename. --regex repeats, and the argv loop is hand-rolled;
// flag with a flag.Value slice would express the repetition directly.
func runMatch(args []string) {
	fileName := ""
	var patterns []string
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--file-name":
			i++
			if i >= len(args) {
				fatalMatchUsage()
			}
			fileName = args[i]
		case "--regex":
			i++
			if i >= len(args) {
				fatalMatchUsage()
			}
			patterns = append(patterns, args[i])
		default:
			fatalMatchUsage()
		}
	}
	if fileName == "" || len(patterns) == 0 {
		fatalMatchUsage()
	}
	fileType, sortable, err := logparse.ParseFileTypeAndDateFromRegexes(fileName, patterns)
	if err != nil {
		os.Exit(1)
	}
	fmt.Print(logparse.FormatParseResult(fileType, sortable))
}

// runMatchBatch classifies filenames streamed on stdin, one result per line. Nothing in the repo
// calls it.
func runMatchBatch(args []string) {
	var patterns []string
	for i := 0; i < len(args); i++ {
		if args[i] != "--regex" {
			fmt.Fprintln(os.Stderr, "match-batch only supports --regex PATTERN")
			os.Exit(2)
		}
		i++
		if i >= len(args) {
			fmt.Fprintln(os.Stderr, "missing value for --regex")
			os.Exit(2)
		}
		patterns = append(patterns, args[i])
	}
	if len(patterns) == 0 {
		fmt.Fprintln(os.Stderr, "match-batch requires at least one --regex")
		os.Exit(2)
	}
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		fileType, sortable, err := logparse.ParseFileTypeAndDateFromRegexes(line, patterns)
		if err != nil {
			continue
		}
		fmt.Println(logparse.FormatParseResult(fileType, sortable))
	}
	if err := scanner.Err(); err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		os.Exit(1)
	}
}

func fatalMatchUsage() {
	fmt.Fprintln(os.Stderr, "match requires --file-name NAME and at least one --regex PATTERN")
	os.Exit(2)
}
