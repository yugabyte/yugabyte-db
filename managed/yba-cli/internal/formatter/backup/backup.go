/*
* Copyright (c) YugaByte, Inc.
 */

package backup

import (
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultBackupListing = "table {{.BackupUUID}}\t{{.BaseBackupUUID}}\t{{.Universe}}" +
		"\t{{.StorageConfig}}\t{{.StorageConfigType}}\t{{.BackupType}}" +
		"\t{{.State}}\t{{.CompletionTime}}"

	backupUUIDHeader     = "Backup UUID"
	baseBackupUUIDHeader = "Base Backup UUID"
	// UniverseHeader to display universe UUID and Name
	UniverseHeader              = "Universe"
	storageConfigHeader         = "Storage Configuration"
	storageConfigTypeHeader     = "Storage Configuration Type"
	backupTypeHeader            = "Backup Type"
	scheduleNameHeader          = "Schedule Name"
	hasIncrementalBackupsHeader = "Has Incremental Backups"
	stateHeader                 = "State"
	expiryTimeHeader            = "Expiry Time"
	createTimeHeader            = "Create Time"
	completionTimeHeader        = "Completion Time"
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
	case "table", "":
		format := defaultBackupListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of backups
func Write(ctx formatter.Context, backups []ybaclient.BackupResp) error {
	// Check if the format is JSON or Pretty JSON
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
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
		"BackupUUID":            backupUUIDHeader,
		"BaseBackupUUID":        baseBackupUUIDHeader,
		"Universe":              UniverseHeader,
		"StorageConfig":         storageConfigHeader,
		"StorageConfigType":     storageConfigTypeHeader,
		"BackupType":            backupTypeHeader,
		"ScheduleName":          scheduleNameHeader,
		"HasIncrementalBackups": hasIncrementalBackupsHeader,
		"State":                 stateHeader,
		"ExpiryTime":            expiryTimeHeader,
		"CreateTime":            createTimeHeader,
		"CompletionTime":        completionTimeHeader,
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

// StorageConfig fetches Storage Config Name
func (c *Context) StorageConfig() string {
	commonBackupInfo := c.b.GetCommonBackupInfo()
	for _, config := range StorageConfigs {
		if strings.Compare(config.GetConfigUUID(), commonBackupInfo.GetStorageConfigUUID()) == 0 {
			return fmt.Sprintf("%s(%s)", config.GetConfigName(), config.GetConfigUUID())
		}
	}
	// get name too
	return commonBackupInfo.GetStorageConfigUUID()
}

// ExpiryTime fetches Expiry Time
func (c *Context) ExpiryTime() string {
	expiryTime := c.b.GetExpiryTime()
	if expiryTime.IsZero() {
		return ""
	} else {
		return expiryTime.Format(time.RFC1123Z)
	}
}

// BackupType fetches Backup Type
func (c *Context) BackupType() string {
	return c.b.GetBackupType()
}

// ScheduleName fetches Schedule Name
func (c *Context) ScheduleName() string {
	return c.b.GetScheduleName()
}

// StorageConfigType fetches Storage Config Type
func (c *Context) StorageConfigType() string {
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
	return c.b.GetCommonBackupInfo().CreateTime.Format(time.RFC1123Z)
}

// CompletionTime fetches Completion Time
func (c *Context) CompletionTime() string {
	completionTime := c.b.GetCommonBackupInfo().CompletionTime
	if completionTime == nil {
		return ""
	} else {
		return completionTime.Format(time.RFC1123Z)
	}
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.b)
}
