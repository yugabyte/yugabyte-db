// Copyright (c) YugaByte, Inc.

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
	expectedChecksum := "3590b9bbd432f7a9f2db8c69c7313a70c03e1ac6cfa52dfdc1ea6c2b4dfeb8fc" // sha256sum of "yugabyte"

	// Write test content to file
	if err := os.WriteFile(tmpFile, []byte(content), 0644); err != nil {
		t.Fatalf("failed to write temp file: %v", err)
	}

	t.Run("invalid checksum", func(t *testing.T) {
		badChecksum := strings.Repeat("0", 64)
		if err := VerifyChecksum(tmpFile, badChecksum); err == nil {
			t.Errorf("expected checksum mismatch error but got none")
		}
	})

	t.Run("missing file", func(t *testing.T) {
		err := VerifyChecksum(filepath.Join(tmpDir, "nonexistent.txt"), expectedChecksum)
		if err == nil {
			t.Errorf("expected error for missing file but got none")
		}
	})
}
