/*
* Copyright (c) YugabyteDB, Inc.
 */

package backup

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultBackupListing = "table {{.BackupUUID}}\t{{.Universe}}" +
		"\t{{.StorageConfiguration}}\t{{.StorageConfigurationType}}\t{{.KMSConfig}}\t{{.BackupType}}" +
		"\t{{.CompletionTime}}\t{{.HasIncrementalBackups}}\t{{.State}}"

	// BackupUUIDHeader to display backup UUID
	BackupUUIDHeader     = "Backup UUID"
	baseBackupUUIDHeader = "Base Backup UUID"
	// UniverseHeader to display universe UUID and Name
	UniverseHeader = "Universe"
	// StorageConfigHeader to display storage config
	StorageConfigHeader     = "Storage Configuration"
	storageConfigTypeHeader = "Storage Configuration Type"
	// BackupTypeHeader to display backup type
	BackupTypeHeader            = "Backup Type"
	scheduleNameHeader          = "Schedule Name"
	hasIncrementalBackupsHeader = "Has Incremental Backups"
	// StateHeader to display state
	StateHeader      = "State"
	expiryTimeHeader = "Expiry Time"
	// CompletionTimeHeader to display completion time
	CompletionTimeHeader = "Completion Time"
	categoryHeader       = "Category"
)

// Context for BackupResp outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	b ybaclient.BackupResp
}

// NewBackupFormat for formatting output
func NewBackupFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultBackupListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of backups
func Write(ctx formatter.Context, backups []ybaclient.BackupResp) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of backups into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(backups, "", "  ")
		} else {
			output, err = json.Marshal(backups)
		}

		if err != nil {
			logrus.Errorf("Error marshaling backups to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, backup := range backups {
			err := format(&Context{b: backup})
			if err != nil {
				logrus.Debugf("Error rendering backup: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewBackupContext(), render)

}

// NewBackupContext creates a new context for rendering backups
func NewBackupContext() *Context {
	backupCtx := Context{}
	backupCtx.Header = formatter.SubHeaderContext{
		"BackupUUID":               BackupUUIDHeader,
		"BaseBackupUUID":           baseBackupUUIDHeader,
		"Universe":                 UniverseHeader,
		"StorageConfiguration":     StorageConfigHeader,
		"StorageConfigurationType": storageConfigTypeHeader,
		"BackupType":               BackupTypeHeader,
		"ScheduleName":             scheduleNameHeader,
		"HasIncrementalBackups":    hasIncrementalBackupsHeader,
		"State":                    StateHeader,
		"ExpiryTime":               expiryTimeHeader,
		"CreateTime":               formatter.CreateTimeHeader,
		"CompletionTime":           CompletionTimeHeader,
		"KMSConfig":                formatter.KMSConfigHeader,
		"Category":                 categoryHeader,
	}
	return &backupCtx
}

// BackupUUID fetches Backup UUID
func (c *Context) BackupUUID() string {
	commonBackupInfo := c.b.GetCommonBackupInfo()
	return commonBackupInfo.GetBackupUUID()
}

// BaseBackupUUID fetches Base Backup UUID
func (c *Context) BaseBackupUUID() string {
	commonBackupInfo := c.b.GetCommonBackupInfo()
	return commonBackupInfo.GetBackupUUID()
}

// StorageConfiguration fetches Storage Config Name
func (c *Context) StorageConfiguration() string {
	commonBackupInfo := c.b.GetCommonBackupInfo()
	for _, config := range StorageConfigs {
		if strings.Compare(config.GetConfigUUID(), commonBackupInfo.GetStorageConfigUUID()) == 0 {
			return fmt.Sprintf("%s(%s)", config.GetConfigName(), config.GetConfigUUID())
		}
	}
	// get name too
	return commonBackupInfo.GetStorageConfigUUID()
}

// KMSConfig fetches KMS Config
func (c *Context) KMSConfig() string {
	commonBackupInfo := c.b.GetCommonBackupInfo()
	for _, k := range KMSConfigs {
		if len(strings.TrimSpace(k.ConfigUUID)) != 0 &&
			strings.Compare(k.ConfigUUID, commonBackupInfo.GetKmsConfigUUID()) == 0 {
			return fmt.Sprintf("%s(%s)", k.Name, commonBackupInfo.GetKmsConfigUUID())
		}
	}
	return commonBackupInfo.GetKmsConfigUUID()
}

// ExpiryTime fetches Expiry Time
func (c *Context) ExpiryTime() string {
	return util.PrintTime(c.b.GetExpiryTime())
}

// Category fetches Category
func (c *Context) Category() string {
	return c.b.GetCategory()
}

// BackupType fetches Backup Type
func (c *Context) BackupType() string {
	return c.b.GetBackupType()
}

// ScheduleName fetches Schedule Name
func (c *Context) ScheduleName() string {
	return c.b.GetScheduleName()
}

// StorageConfigurationType fetches Storage Config Type
func (c *Context) StorageConfigurationType() string {
	return c.b.GetStorageConfigType()
}

// HasIncrementalBackups fetches Has Incremental Backups
func (c *Context) HasIncrementalBackups() string {
	return fmt.Sprintf("%t", c.b.HasIncrementalBackups)
}

// State fetches State
func (c *Context) State() string {
	commonBackupInfo := c.b.GetCommonBackupInfo()
	state := commonBackupInfo.GetState()
	if strings.Compare(state, util.CompletedBackupState) == 0 ||
		strings.Compare(state, util.StoppedBackupState) == 0 {
		return formatter.Colorize(state, formatter.GreenColor)
	}
	if strings.Compare(state, util.FailedBackupState) == 0 ||
		strings.Compare(state, util.FailedToDeleteBackupState) == 0 {
		return formatter.Colorize(state, formatter.RedColor)
	}
	return formatter.Colorize(state, formatter.YellowColor)

}

// Universe fetches Universe UUID and name
func (c *Context) Universe() string {
	return fmt.Sprintf("%s(%s)", c.b.GetUniverseName(), c.b.GetUniverseUUID())
}

// CreateTime fetches Create Time
func (c *Context) CreateTime() string {
	commonBackupInfo := c.b.GetCommonBackupInfo()
	return util.PrintTime(commonBackupInfo.GetCreateTime())
}

// CompletionTime fetches Completion Time
func (c *Context) CompletionTime() string {
	commonBackupInfo := c.b.GetCommonBackupInfo()
	return util.PrintTime(commonBackupInfo.GetCompletionTime())
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.b)
}
