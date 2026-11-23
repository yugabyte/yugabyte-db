// Copyright (c) YugabyteDB, Inc.

package logparse_test

import (
	"encoding/base64"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/devops/supportbundle/logparse"
)

const (
	universeLogsPattern = "((?:.*)(?:yb-)(?:master|tserver)(?!.*WARNING)(?:.*))(\\d{8}-\\d{6})\\.(?:.*)"
	postgresLogsPattern = "((?:.*)(?:postgresql)-)(\\d{4}-\\d{2}-\\d{2}_\\d{6})(?:.*)"
)

func TestDecodeBase64String(t *testing.T) {
	encoded := base64.StdEncoding.EncodeToString([]byte(universeLogsPattern))
	got, err := logparse.DecodeBase64String(encoded)
	if err != nil {
		t.Fatalf("DecodeBase64String() error = %v", err)
	}
	if got != universeLogsPattern {
		t.Fatalf("DecodeBase64String() = %q, want %q", got, universeLogsPattern)
	}
}

func TestCompactDateAddDays(t *testing.T) {
	got, err := logparse.CompactDateAddDays("20260707", "-7")
	if err != nil {
		t.Fatalf("CompactDateAddDays() error = %v", err)
	}
	if got != "20260630" {
		t.Fatalf("CompactDateAddDays() = %q, want 20260630", got)
	}
}

func TestNormalizeDateToSortable(t *testing.T) {
	got, err := logparse.NormalizeDateToSortable("2026-07-06T17:09:39Z")
	if err != nil {
		t.Fatalf("NormalizeDateToSortable() error = %v", err)
	}
	if got != "20260706170939" {
		t.Fatalf("NormalizeDateToSortable() = %q, want 20260706170939", got)
	}
}

func TestNormalizeDateToCompact(t *testing.T) {
	got, err := logparse.NormalizeDateToCompact("2026-07-06T17:09:39Z")
	if err != nil {
		t.Fatalf("NormalizeDateToCompact() error = %v", err)
	}
	if got != "20260706" {
		t.Fatalf("NormalizeDateToCompact() = %q, want 20260706", got)
	}
}

func TestParseFileTypeAndDateFromRegexes(t *testing.T) {
	fileName := "yb-tserver.n1.yb-tserver.yb-tserver.out.20260706-170939.12345"
	fileType, sortable, err := logparse.ParseFileTypeAndDateFromRegexes(
		fileName,
		[]string{universeLogsPattern},
	)
	if err != nil {
		t.Fatalf("ParseFileTypeAndDateFromRegexes() error = %v", err)
	}
	if sortable != "20260706170939" {
		t.Fatalf("sortable = %q, want 20260706170939", sortable)
	}
	if fileType == "" {
		t.Fatal("fileType should not be empty")
	}

	pgName := "postgresql-2026-07-06_170939.log"
	_, pgSortable, err := logparse.ParseFileTypeAndDateFromRegexes(
		pgName,
		[]string{postgresLogsPattern},
	)
	if err != nil {
		t.Fatalf("postgres ParseFileTypeAndDateFromRegexes() error = %v", err)
	}
	if pgSortable != "20260706170939" {
		t.Fatalf("postgres sortable = %q, want 20260706170939", pgSortable)
	}
}
