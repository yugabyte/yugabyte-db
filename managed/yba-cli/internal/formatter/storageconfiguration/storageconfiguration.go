/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/aws"
)

const (
	defaultStorageConfigListing = "table {{.Name}}\t{{.Code}}\t{{.UUID}}" +
		"\t{{.BackupLocation}}\t{{.Status}}"

	backupLocationHeader     = "Backup Location"
	iamInstanceProfileHeader = "IAM Instance Profile Enabled"
	useGCPIAMHeader          = "Use GCP IAM Enabled"
	gcsCredentialsHeader     = "GCS Credentials"
	azSASTokenHeader         = "Azure SAS Token"
)

// Context for storageConfig outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	s ybaclient.CustomerConfigUI
}

// NewStorageConfigFormat for formatting output
func NewStorageConfigFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultStorageConfigListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of StorageConfigs
func Write(ctx formatter.Context, storageConfigs []ybaclient.CustomerConfigUI) error {
	// Check if the format is JSON or Pretty JSON
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		// Marshal the slice of storage configurations into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(storageConfigs, "", "  ")
		} else {
			output, err = json.Marshal(storageConfigs)
		}

		if err != nil {
			logrus.Errorf("Error marshaling storage configurations to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, storageConfig := range storageConfigs {
			err := format(&Context{s: storageConfig})
			if err != nil {
				logrus.Debugf("Error rendering storage configuration: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewStorageConfigContext(), render)
}

// NewStorageConfigContext creates a new context for rendering storageConfig
func NewStorageConfigContext() *Context {
	storageConfigCtx := Context{}
	storageConfigCtx.Header = formatter.SubHeaderContext{
		"Name":           formatter.NameHeader,
		"UUID":           formatter.UUIDHeader,
		"BackupLocation": backupLocationHeader,
		"Status":         formatter.StatusHeader,
		"Code":           formatter.CodeHeader,

		"S3AccessKeyID":      aws.AccessKeyIDHeader,
		"S3SecretAccessKey":  aws.AccessKeySecretHeader,
		"IAMInstanceProfile": iamInstanceProfileHeader,

		"UseGCPIAM": useGCPIAMHeader,

		"AzSASToken": azSASTokenHeader,
	}
	return &storageConfigCtx
}

// UUID fetches StorageConfig UUID
func (c *Context) UUID() string {
	return c.s.GetConfigUUID()
}

// Name fetches StorageConfig Name
func (c *Context) Name() string {
	return c.s.GetConfigName()
}

// Code fetches StorageConfig Code
func (c *Context) Code() string {
	return c.s.GetName()
}

// Status fetches the storageConfig usability state
func (c *Context) Status() string {
	if c.s.GetInUse() {
		return "In Use"
	}
	return "Not in use"

}

// BackupLocation of the storageConfig
func (c *Context) BackupLocation() string {
	data := c.s.GetData()
	if location, ok := data["BACKUP_LOCATION"]; ok {
		return location.(string)
	}
	return "-"
}

// S3AccessKeyID of the storage config
func (c *Context) S3AccessKeyID() string {
	data := c.s.GetData()
	if accessKeyID, ok := data[util.AWSAccessKeyEnv]; ok {
		return accessKeyID.(string)
	}
	return "-"
}

// S3SecretAccessKey of the storage config
func (c *Context) S3SecretAccessKey() string {
	data := c.s.GetData()
	if secretAccessKey, ok := data[util.AWSSecretAccessKeyEnv]; ok {
		return secretAccessKey.(string)
	}
	return "-"
}

// IAMInstanceProfile of the storage config
func (c *Context) IAMInstanceProfile() string {
	data := c.s.GetData()
	if iamInstanceProfile, ok := data[util.IAMInstanceProfile]; ok {
		return iamInstanceProfile.(string)
	}
	return "-"
}

// UseGCPIAM of the storage config
func (c *Context) UseGCPIAM() string {
	data := c.s.GetData()
	if useGCPIAM, ok := data[util.UseGCPIAM]; ok {
		return useGCPIAM.(string)
	}
	return "-"
}

// GcsCredentials of the storage config
func (c *Context) GcsCredentials() string {
	data := c.s.GetData()
	if gcsCredentialsJSON, ok := data[util.GCSCredentialsJSON]; ok {
		return gcsCredentialsJSON.(string)
	}
	return "-"
}

// AzSASToken of the storage config
func (c *Context) AzSASToken() string {
	data := c.s.GetData()
	if azureStorageSasToken, ok := data[util.AzureStorageSasTokenEnv]; ok {
		return azureStorageSasToken.(string)
	}
	return "-"
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.s)
}
