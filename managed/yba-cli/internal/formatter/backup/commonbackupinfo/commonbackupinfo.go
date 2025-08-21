/*
* Copyright (c) YugaByte, Inc.
 */

package commonbackupinfo

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup"
)

const (
	defaultCommonBackupInfoListing = "table {{.BackupUUID}}\t{{.StorageConfig}}\t{{.State}}"
	commonBackupInfoDetails1       = "table {{.TableByTableBackup}}\t{{.TotalBackupSizeInBytes}}"
	commonBackupInfoDetails2       = "table {{.CreateTime}}\t{{.UpdateTime}}\t{{.CompletionTime}}"

	tableByTableBackupHeader     = "Is Table by Table"
	totalBackupSizeInBytesHeader = "Total Backup Size"
	updateTimeHeader             = "Update Time"
)

// StorageConfigs hold storage config for the backup
var StorageConfigs []ybaclient.CustomerConfigUI

// Context for commonBackupInfo outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	c ybaclient.CommonBackupInfo
}

// NewCommonBackupInfoFormat for formatting output
func NewCommonBackupInfoFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultCommonBackupInfoListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of CommonBackupInfos
func Write(ctx formatter.Context, commonBackupInfos []ybaclient.CommonBackupInfo) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of commonBackupInfos into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(commonBackupInfos, "", "  ")
		} else {
			output, err = json.Marshal(commonBackupInfos)
		}

		if err != nil {
			logrus.Errorf("Error marshaling common backup infos to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, commonBackupInfo := range commonBackupInfos {
			err := format(&Context{c: commonBackupInfo})
			if err != nil {
				logrus.Debugf("Error rendering common backup info: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewCommonBackupInfoContext(), render)
}

// NewCommonBackupInfoContext creates a new context for rendering common Backup Info
func NewCommonBackupInfoContext() *Context {
	commonBackupInfoCtx := Context{}
	commonBackupInfoCtx.Header = formatter.SubHeaderContext{
		"BackupUUID":             backup.BackupUUIDHeader,
		"State":                  backup.StateHeader,
		"StorageConfig":          backup.StorageConfigHeader,
		"TableByTableBackup":     tableByTableBackupHeader,
		"TotalBackupSizeInBytes": totalBackupSizeInBytesHeader,
		"CreateTime":             formatter.CreateTimeHeader,
		"UpdateTime":             formatter.UpdateTimeHeader,
		"CompletionTime":         backup.CompletionTimeHeader,
	}
	return &commonBackupInfoCtx
}

// BackupUUID fetches Backup UUID
func (c *Context) BackupUUID() string {
	return c.c.GetBackupUUID()
}

// State fetches Backup State
func (c *Context) State() string {
	state := c.c.GetState()
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

// StorageConfig fetches Backup StorageConfig
func (c *Context) StorageConfig() string {
	for _, config := range StorageConfigs {
		if strings.Compare(config.GetConfigUUID(), c.c.GetStorageConfigUUID()) == 0 {
			return fmt.Sprintf("%s(%s)", config.GetConfigName(), config.GetConfigUUID())
		}
	}
	return c.c.GetStorageConfigUUID()
}

// TableByTableBackup fetches whether Backup is TableByTableBackup
func (c *Context) TableByTableBackup() bool {
	return c.c.GetTableByTableBackup()
}

// TotalBackupSizeInBytes fetches whether Backup TotalBackupSizeInBytes
func (c *Context) TotalBackupSizeInBytes() string {
	size, unit := util.HumanReadableSize(float64(c.c.GetTotalBackupSizeInBytes()))
	return fmt.Sprintf("%0.2f %s", size, unit)
}

// CreateTime fetches whether Backup CreateTime
func (c *Context) CreateTime() string {
	return util.PrintTime(c.c.GetCreateTime())
}

// UpdateTime fetches whether Backup UpdateTime
func (c *Context) UpdateTime() string {
	return util.PrintTime(c.c.GetUpdateTime())
}

// CompletionTime fetches whether Backup CompletionTime
func (c *Context) CompletionTime() string {
	return util.PrintTime(c.c.GetCompletionTime())
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.c)
}
