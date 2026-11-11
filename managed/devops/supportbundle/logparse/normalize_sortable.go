// Copyright (c) YugabyteDB, Inc.

package logparse

import (
	"fmt"
	"regexp"
	"strings"
	"time"
)

var (
	sortableDateTimePattern = regexp.MustCompile(`(\d{4}-\d{2}-\d{2})[T _](\d{2}):(\d{2}):(\d{2})`)
	compactDashTimePattern  = regexp.MustCompile(`(\d{8}-\d{6})`)
)

var sortableParseLayouts = []string{
	"2006-01-02T15:04:05Z",
	"2006-01-02T15:04:05",
	"2006-01-02 15:04:05",
	"2006-01-02",
	"20060102-150405",
	"20060102",
}

const sortableLayout = "20060102150405"

// NormalizeDateToSortable parses mixed date inputs into YYYYMMDDHHMMSS UTC (mirrors normalize_date_to_sortable).
func NormalizeDateToSortable(raw string) (string, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return "", fmt.Errorf("empty date value")
	}

	for _, layout := range sortableParseLayouts {
		if parsed, err := time.Parse(layout, raw); err == nil {
			return parsed.UTC().Format(sortableLayout), nil
		}
	}

	if isAllDigits(raw) && len(raw) == 14 {
		return raw, nil
	}

	if isAllDigits(raw) && len(raw) == 8 {
		if parsed, err := time.Parse(compactDateLayout, raw); err == nil {
			return parsed.UTC().Format(sortableLayout), nil
		}
	}

	if match := sortableDateTimePattern.FindStringSubmatch(raw); len(match) == 5 {
		day := strings.ReplaceAll(match[1], "-", "")
		return day + match[2] + match[3] + match[4], nil
	}

	if match := compactDashTimePattern.FindStringSubmatch(raw); len(match) == 2 {
		return strings.ReplaceAll(match[1], "-", ""), nil
	}

	if match := isoDatePattern.FindString(raw); match != "" {
		return strings.ReplaceAll(match, "-", "") + "000000", nil
	}

	if match := eightDigitPattern.FindString(raw); match != "" {
		return match + "000000", nil
	}

	return "", fmt.Errorf("unable to normalize date %q to sortable form", raw)
}
