// Copyright (c) YugabyteDB, Inc.

package logparse

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"
	"time"
)

var (
	eightDigitPattern = regexp.MustCompile(`\d{8}`)
	isoDatePattern    = regexp.MustCompile(`\d{4}-\d{2}-\d{2}`)
)

// NormalizeDateToCompact parses mixed date inputs into YYYYMMDD (mirrors normalize_date_to_compact).
func NormalizeDateToCompact(raw string) (string, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return "", fmt.Errorf("empty date value")
	}

	if isAllDigits(raw) && len(raw) == 8 {
		if _, err := time.Parse(compactDateLayout, raw); err == nil {
			return raw, nil
		}
	}

	if isAllDigits(raw) {
		value, err := strconv.ParseInt(raw, 10, 64)
		if err != nil {
			return "", err
		}
		if len(raw) >= 13 {
			value = value / 1000
		}
		t := time.Unix(value, 0).UTC()
		return t.Format(compactDateLayout), nil
	}

	if match := eightDigitPattern.FindString(raw); match != "" {
		return match, nil
	}

	if match := isoDatePattern.FindString(raw); match != "" {
		return strings.ReplaceAll(match, "-", ""), nil
	}

	return "", fmt.Errorf("unable to normalize date %q to compact form", raw)
}

func isAllDigits(value string) bool {
	if value == "" {
		return false
	}
	for _, r := range value {
		if r < '0' || r > '9' {
			return false
		}
	}
	return true
}
