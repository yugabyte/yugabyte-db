/*
 * Copyright (c) YugabyteDB, Inc.
 */

package storageconfigurationutil

import (
	"slices"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestStorageConfigNotFoundMessage(t *testing.T) {
	got := storageConfigNotFoundMessage("backup", "Azure")
	want := "No Azure storage configurations with name: backup found\n"
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
	got = storageConfigNotFoundMessage("backup", "")
	want = "No storage configurations with name: backup found\n"
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestFilterStorageConfigs(t *testing.T) {
	config := func(uuid, configType, code, name string) ybaclient.CustomerConfigUI {
		return ybaclient.CustomerConfigUI{
			ConfigUUID: &uuid,
			Type:       configType,
			Name:       code,
			ConfigName: name,
		}
	}
	configs := []ybaclient.CustomerConfigUI{
		config("s3-backup", util.StorageCustomerConfigType, util.S3StorageConfigType, "backup"),
		config("oci-backup", util.StorageCustomerConfigType, util.OCIStorageConfigType, "backup"),
		config("az-backup", util.StorageCustomerConfigType, util.AzureStorageConfigType,
			"backup"),
		config("gcs-other", util.StorageCustomerConfigType, util.GCSStorageConfigType, "other"),
		config("alert-backup", "ALERTS", "SMTP", "backup"),
	}
	tests := []struct {
		name        string
		configName  string
		storageCode string
		want        []string
	}{
		{
			name:       "any storage code",
			configName: "backup",
			want:       []string{"s3-backup", "oci-backup", "az-backup"},
		},
		{
			name:        "OCI code skips S3 config with same name",
			configName:  "backup",
			storageCode: util.OCIStorageConfigType,
			want:        []string{"oci-backup"},
		},
		{
			name:        "S3 code",
			configName:  "backup",
			storageCode: util.S3StorageConfigType,
			want:        []string{"s3-backup"},
		},
		{
			name:        "Azure code",
			configName:  "backup",
			storageCode: util.AzureStorageConfigType,
			want:        []string{"az-backup"},
		},
		{
			name:        "storage code compared case-insensitively",
			configName:  "backup",
			storageCode: "oci",
			want:        []string{"oci-backup"},
		},
		{
			name:        "no config of that code with name",
			configName:  "backup",
			storageCode: util.NFSStorageConfigType,
			want:        []string{},
		},
		{
			name:        "empty name filters by code only",
			storageCode: util.GCSStorageConfigType,
			want:        []string{"gcs-other"},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got := make([]string, 0)
			for _, s := range FilterStorageConfigs(configs, tc.configName, tc.storageCode) {
				got = append(got, s.GetConfigUUID())
			}
			if !slices.Equal(got, tc.want) {
				t.Fatalf("got %v, want %v", got, tc.want)
			}
		})
	}
}
