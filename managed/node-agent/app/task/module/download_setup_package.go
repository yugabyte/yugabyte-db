// Copyright (c) YugaByte, Inc.

package module

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	pb "node-agent/generated/service"
	"os"
	"strings"
)

// downloadSoftwareFromS3 returns the shell command to download from S3 using s3cmd.
func downloadSoftwareFromS3(
	awsAccessKey, awsSecretKey, s3PackagePath, tmpDownloadDir string,
) (string, error) {
	if awsAccessKey == "" || awsSecretKey == "" {
		return "", errors.New("AWS credentials are not specified")
	}
	_ = os.Setenv("AWS_ACCESS_KEY_ID", awsAccessKey)
	_ = os.Setenv("AWS_SECRET_ACCESS_KEY", awsSecretKey)
	return fmt.Sprintf("s3cmd get %s %s", s3PackagePath, tmpDownloadDir), nil
}

// downloadSoftwareFromGCS returns the shell command to download from GCS using gsutil.
func downloadSoftwareFromGCS(
	gcpCredentialsJson, gcpPackagePath, tmpDownloadDir string,
) (string, error) {
	if gcpCredentialsJson == "" {
		return "", errors.New("GCP credentials are not specified")
	}
	return fmt.Sprintf("gsutil -o Credentials:gs_service_key_file=%s cp %s %s",
		gcpCredentialsJson, gcpPackagePath, tmpDownloadDir), nil
}

// downloadSoftwareFromHTTP returns the shell command to download from HTTP using curl (prefered over wget).
func downloadSoftwareFromHTTP(httpPackagePath, tmpDownloadDir string) (string, error) {
	return fmt.Sprintf("curl -L -o %s %s", tmpDownloadDir, httpPackagePath), nil
}

// DownloadSoftwareCommand returns the download command based on the input parameters.
func DownloadSoftwareCommand(
	params *pb.InstallSoftwareInput,
	tmpDownloadDir string,
) (string, error) {
	switch {
	case params.GetS3RemoteDownload():
		return downloadSoftwareFromS3(
			params.GetAwsAccessKey(),
			params.GetAwsSecretKey(),
			params.GetYbPackage(),
			tmpDownloadDir,
		)
	case params.GetGcsRemoteDownload():
		return downloadSoftwareFromGCS(
			params.GetGcsCredentialsJson(),
			params.GetYbPackage(),
			tmpDownloadDir,
		)
	case params.GetHttpRemoteDownload():
		return downloadSoftwareFromHTTP(
			params.GetYbPackage(),
			tmpDownloadDir,
		)
	default:
		return "", nil
	}
}

// VerifyChecksum checks the SHA256 checksum of a file against an expected value.
func VerifyChecksum(filePath, expectedChecksum string) error {
	file, err := os.Open(filePath)
	if err != nil {
		return fmt.Errorf("error opening file %s: %w", filePath, err)
	}
	defer file.Close()

	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return fmt.Errorf("error calculating checksum for %s: %w", filePath, err)
	}
	actualChecksum := hex.EncodeToString(hash.Sum(nil))

	if !strings.EqualFold(actualChecksum, expectedChecksum) {
		return fmt.Errorf(
			"checksum mismatch for %s: expected %s, got %s",
			filePath,
			expectedChecksum,
			actualChecksum,
		)
	}
	return nil
}
