// Copyright (c) YugabyteDB, Inc.

package logparse

import (
	"fmt"
	"strings"
	"time"

	"github.com/dlclark/regexp2"
)

// Date layouts accepted in a log filename, tried in order against each capture group.
var logFileDateLayouts = []string{
	"20060102-150405",
	"2006-01-02_150405",
	"2006-01-02",
	"20060102",
}

// ParseFileTypeAndDateFromRegexes matches fileName against patterns and returns the file type and
// a sortable timestamp.
//
// Capture group 1 is the file type by convention. The remaining groups are swept in order and the
// first one that parses under any logFileDateLayouts entry becomes the timestamp, so a pattern
// need not put the date in a fixed position.
//
// Fullmatch is enforced by hand — regexp2 has no fullmatch entry point, so a match is rejected
// unless it starts at 0 and spans the whole name. This mirrors Python's re.fullmatch, which the
// inline-Python fallback in node_utils.sh uses; a partial match here would classify files that
// the fallback rejects, making collection depend on whether the binary was deployed.
func ParseFileTypeAndDateFromRegexes(fileName string, patterns []string) (fileType string, sortable string, err error) {
	if len(patterns) == 0 {
		return "", "", fmt.Errorf("no regex patterns provided")
	}

	for _, pattern := range patterns {
		if strings.TrimSpace(pattern) == "" {
			continue
		}
		re, compileErr := regexp2.Compile(pattern, 0)
		if compileErr != nil {
			continue
		}
		match, matchErr := re.FindStringMatch(fileName)
		if matchErr != nil || match == nil {
			continue
		}
		if match.Index != 0 || match.Length != len(fileName) {
			continue
		}

		candidateType := ""
		if group := match.GroupByNumber(1); group != nil {
			candidateType = group.String()
		}

		candidateTimestamp := ""
		for i := 1; i <= match.GroupCount(); i++ {
			group := match.GroupByNumber(i)
			if group == nil {
				continue
			}
			if normalized := toSortableTimestamp(group.String()); normalized != "" {
				candidateTimestamp = normalized
				break
			}
		}

		if candidateType != "" && candidateTimestamp != "" {
			return candidateType, candidateTimestamp, nil
		}
	}

	return "", "", fmt.Errorf("no pattern matched file name %q", fileName)
}

// FormatParseResult returns the bash-compatible "fileType|timestamp" output.
func FormatParseResult(fileType, sortable string) string {
	return fileType + "|" + sortable
}

func toSortableTimestamp(value string) string {
	value = strings.TrimSpace(value)
	if value == "" {
		return ""
	}
	for _, layout := range logFileDateLayouts {
		if parsed, err := time.Parse(layout, value); err == nil {
			return parsed.UTC().Format(sortableLayout)
		}
	}
	return ""
}
