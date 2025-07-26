package testutils

import (
	"io"
	"os"
	"os/exec"
	"testing"
)

func ExtractTgzPackage(tb testing.TB, tgzPath, destDir string) {
	tb.Helper()
	tb.Logf("Extracting %s to %s", tgzPath, destDir)
	cmd := exec.Command("tar", "-xf", tgzPath, "-C", destDir)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		tb.Fatalf("failed to extract %s: %v", tgzPath, err)
	}
}

func DownloadS3Package(tb testing.TB, bucket, key, dest string) {
	tb.Helper()
	tb.Logf("Downloading %s from bucket %s to %s", key, bucket, dest)
	client := NewS3Client()
	reader, err := client.Download(bucket, key)
	if err != nil {
		tb.Fatalf("failed to download %s from bucket %s: %v", key, bucket, err)
	}
	f, err := os.Create(dest)
	if err != nil {
		tb.Fatalf("failed to create file %s: %v", dest, err)
	}
	defer f.Close()
	if _, err := io.Copy(f, reader); err != nil {
		tb.Fatalf("failed to copy data from S3 to %s: %v", dest, err)
	}
	tb.Logf("Successfully downloaded %s to %s", key, dest)
}
