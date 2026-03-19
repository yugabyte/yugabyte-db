// Copyright (c) YugabyteDB, Inc.

package module

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestDownloadSoftwareFromS3(t *testing.T) {
	tests := []struct {
		name           string
		awsAccessKey   string
		awsSecretKey   string
		s3PackagePath  string
		tmpDownloadDir string
		wantCmd        string
		wantErr        bool
	}{
		{
			name:           "valid S3 download command",
			awsAccessKey:   "AKIA123",
			awsSecretKey:   "SECRET123",
			s3PackagePath:  "s3://bucket/yugabyte.tar.gz",
			tmpDownloadDir: "/tmp",
			wantCmd:        "s3cmd get s3://bucket/yugabyte.tar.gz /tmp",
			wantErr:        false,
		},
		{
			name:           "missing AWS keys",
			awsAccessKey:   "",
			awsSecretKey:   "",
			s3PackagePath:  "s3://bucket/yugabyte.tar.gz",
			tmpDownloadDir: "/tmp",
			wantErr:        true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotCmd, err := downloadSoftwareFromS3(
				tt.awsAccessKey,
				tt.awsSecretKey,
				tt.s3PackagePath,
				tt.tmpDownloadDir,
			)
			if (err != nil) != tt.wantErr {
				t.Fatalf("error = %v, wantErr = %v", err, tt.wantErr)
			}
			if !tt.wantErr && gotCmd != tt.wantCmd {
				t.Errorf("got = %v, want = %v", gotCmd, tt.wantCmd)
			}
		})
	}
}

func TestDownloadSoftwareFromGCS(t *testing.T) {
	tests := []struct {
		name               string
		gcpCredentialsJson string
		gcpPackagePath     string
		tmpDownloadDir     string
		wantCmd            string
		wantErr            bool
	}{
		{
			name:               "valid GCS download command",
			gcpCredentialsJson: "/path/to/creds.json",
			gcpPackagePath:     "gs://bucket/yugabyte.tar.gz",
			tmpDownloadDir:     "/tmp",
			wantCmd:            "gsutil -o Credentials:gs_service_key_file=/path/to/creds.json cp gs://bucket/yugabyte.tar.gz /tmp",
			wantErr:            false,
		},
		{
			name:               "missing GCP creds",
			gcpCredentialsJson: "",
			gcpPackagePath:     "gs://bucket/yugabyte.tar.gz",
			tmpDownloadDir:     "/tmp",
			wantErr:            true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotCmd, err := downloadSoftwareFromGCS(
				tt.gcpCredentialsJson,
				tt.gcpPackagePath,
				tt.tmpDownloadDir,
			)
			if (err != nil) != tt.wantErr {
				t.Fatalf("error = %v, wantErr = %v", err, tt.wantErr)
			}
			if !tt.wantErr && gotCmd != tt.wantCmd {
				t.Errorf("got = %v, want = %v", gotCmd, tt.wantCmd)
			}
		})
	}
}

func TestDownloadSoftwareFromHTTP(t *testing.T) {
	httpURL := "https://example.com/yugabyte.tar.gz"
	tmpDownloadDir := "/tmp/yugabyte.tar.gz"
	wantCmd := "curl -L -o /tmp/yugabyte.tar.gz https://example.com/yugabyte.tar.gz"

	gotCmd, err := downloadSoftwareFromHTTP(httpURL, tmpDownloadDir)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if gotCmd != wantCmd {
		t.Errorf("got = %v, want = %v", gotCmd, wantCmd)
	}
}

func TestVerifyChecksum(t *testing.T) {
	tmpDir := t.TempDir()
	tmpFile := filepath.Join(tmpDir, "testfile.txt")

	content := "yugabyte"
	// Previous has was incorrect but not used
	// [yugabyte-db]$ printf '%s' "yugabyte" | sha256sum
	// 1e1c59f402939ecf9fe7d33589b2a0f243f1663088486044f4d8ef1dd1471ab8  -

	validHash := "1e1c59f402939ecf9fe7d33589b2a0f243f1663088486044f4d8ef1dd1471ab8" // sha256 of "yugabyte"

	if err := os.WriteFile(tmpFile, []byte(content), 0644); err != nil {
		t.Fatalf("failed to write temp file: %v", err)
	}

	t.Run("with lowercase prefix", func(t *testing.T) {
		expected := "sha256:" + validHash
		if err := VerifyChecksum(tmpFile, expected); err != nil {
			t.Errorf("expected success with lowercase sha256: prefix, got: %v", err)
		}
	})

	t.Run("with uppercase prefix", func(t *testing.T) {
		expected := "SHA256:" + validHash
		if err := VerifyChecksum(tmpFile, expected); err != nil {
			t.Errorf("expected success with uppercase SHA256: prefix, got: %v", err)
		}
	})

	t.Run("prefix good but hash malformed", func(t *testing.T) {
		expected := "sha256:" + strings.Repeat("0", 64)
		if err := VerifyChecksum(tmpFile, expected); err == nil {
			t.Error("expected checksum mismatch when hash is malformed/wrong")
		}
	})

	t.Run("malformed prefix", func(t *testing.T) {
		// "sha256" without colon — not stripped, so full string compared to actual hash → mismatch
		expected := "sha256" + validHash
		if err := VerifyChecksum(tmpFile, expected); err == nil {
			t.Error("expected failure when prefix is malformed (no colon)")
		}
	})

	t.Run("prefix other than sha256", func(t *testing.T) {
		// sha512: is not stripped, so expected stays "sha512:..." and won't match actual sha256 hex
		expected := "sha512:" + validHash
		if err := VerifyChecksum(tmpFile, expected); err == nil {
			t.Error("expected failure when prefix is not sha256")
		}
	})

	t.Run("invalid checksum", func(t *testing.T) {
		badChecksum := strings.Repeat("0", 64)
		if err := VerifyChecksum(tmpFile, badChecksum); err == nil {
			t.Errorf("expected checksum mismatch error but got none")
		}
	})

	t.Run("missing file", func(t *testing.T) {
		err := VerifyChecksum(filepath.Join(tmpDir, "nonexistent.txt"), validHash)
		if err == nil {
			t.Errorf("expected error for missing file but got none")
		}
	})
}
