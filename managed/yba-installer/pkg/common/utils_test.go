package common

import (
	"os"
	"testing"
)

func TestParseVersion(t *testing.T) {
	version1, err := NewYBVersion("2.17.1.0-b456")
	if err != nil || len(version1.PublicVersionDigits) != 4 ||
		version1.PublicVersionDigits[1] != 17 || version1.BuildNum != 456 {
		t.Fatalf("failed to parse valid string %s %s", version1, err)
	}

	version2, err := NewYBVersion("2.17.1-b456")
	if err == nil {
		t.Fatalf("parsed invalid string %s", version2)
	}

	version3, err := NewYBVersion("2.17.2.0")
	if err != nil {
		t.Fatalf("error for valid string %s %s", version3, err)
	}

}

func TestCompareVersion(t *testing.T) {

	version1 := "2.17.1.0-b9"
	version2 := "2.17.1.0-b100"
	if !LessVersions(version1, version2) {
		t.Fatalf("invalid compare result for %s %s", version1, version2)
	}

	if LessVersions(version1, version1) {
		t.Fatalf("invalid compare result for equal %s", version1)
	}

	version1 = "2.17.1.9-b9"
	version2 = "2.17.2.0"
	if !LessVersions(version1, version2) {
		t.Fatalf("invalid compare result for %s %s", version1, version2)
	}
}

func TestCompareVersionStablePreviewPanic(t *testing.T) {
	version1 := "2024.1.0.0-b123"
	version2 := "2.23.0.0-b321"
	version3 := "2.18.5.7-b56"
	version4 := "2.17.1.0-b65"
	defer func() {
		if r := recover(); r == nil {
			t.Error("no panic when comparing stable and preview versions")
		}
	}()
	LessVersions(version1, version2)
	LessVersions(version2, version1)
	LessVersions(version3, version4)
	LessVersions(version4, version3)
}

func TestCompareVersionStablePreviewDevMode(t *testing.T) {
	origMode := os.Getenv("YBA_MODE")
	origConfirm := skipConfirmation
	os.Setenv("YBA_MODE", "dev")
	DisableUserConfirm()
	defer func() {
		os.Setenv("YBA_MODE", origMode)
		skipConfirmation = origConfirm
	}()
	version1 := "2024.1.0.0-b123"
	version2 := "2.23.0.0-b321"
	version3 := "2.18.5.7-b56"
	version4 := "2.17.1.0-b65"
	LessVersions(version1, version2)
	LessVersions(version2, version1)
	LessVersions(version3, version4)
	LessVersions(version4, version3)
}
