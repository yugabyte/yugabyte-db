// Copyright (c) YugabyteDB, Inc.

package logparse

import (
	"fmt"
	"strconv"
	"strings"
	"time"
)

const compactDateLayout = "20060102"

// CompactDateAddDays adds a signed day delta to a YYYYMMDD compact date string.
func CompactDateAddDays(compactDate string, deltaDays string) (string, error) {
	compactDate = strings.TrimSpace(compactDate)
	deltaDays = strings.TrimSpace(deltaDays)
	if compactDate == "" || deltaDays == "" {
		return "", fmt.Errorf("compact date and delta are required")
	}
	parsed, err := time.Parse(compactDateLayout, compactDate)
	if err != nil {
		return "", fmt.Errorf("parse compact date %q: %w", compactDate, err)
	}
	delta, err := strconv.Atoi(deltaDays)
	if err != nil {
		return "", fmt.Errorf("parse delta days %q: %w", deltaDays, err)
	}
	return parsed.AddDate(0, 0, delta).Format(compactDateLayout), nil
}
