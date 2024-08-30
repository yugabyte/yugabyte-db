/*
* Copyright (c) YugaByte, Inc.
 */

package backup

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"text/template"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultCommonBackupInfo  = "table {{.BackupUUID}}\t{{.State}}\t{{.StorageConfig}}"
	commonBackupInfoDetails1 = "table {{.TableByTableBackup}}\t{{.TotalBackupSizeInBytes}}"
	commonBackupInfoDetails2 = "table {{.CreateTime}}\t{{.UpdateTime}}\t{{.CompletionTime}}"

	tableByTableBackupHeader     = "Is Table by Table"
	totalBackupSizeInBytesHeader = "Total Backup Size In Bytes"
	updateTimeHeader             = "Update Time"
)

// CommonBackupInfoContext for common backup info
type CommonBackupInfoContext struct {
	formatter.HeaderContext
	formatter.Context
	c ybaclient.CommonBackupInfo
}

// NewCommonBackupInfoFormat for formatting output
func NewCommonBackupInfoFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultCommonBackupInfo
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetCommonBackupInfo initializes the context with the commong backup info
func (cb *CommonBackupInfoContext) SetCommonBackupInfo(commonBackupInfo ybaclient.CommonBackupInfo) {
	cb.c = commonBackupInfo
}

type commonBackupInfoContext struct {
	CommonBackupInfo *CommonBackupInfoContext
}

// Write populates the output table to be displayed in the command line
func (cb *CommonBackupInfoContext) Write(index int) error {
	var err error
	cbc := &commonBackupInfoContext{
		CommonBackupInfo: &CommonBackupInfoContext{},
	}
	cbc.CommonBackupInfo.c = cb.c

	// Section 1
	tmpl, err := cb.startSubsection(defaultCommonBackupInfo)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	cb.Output.Write([]byte(
		formatter.Colorize(
			fmt.Sprintf("Common Backup %d Details", index+1),
			formatter.BlueColor)))
	cb.Output.Write([]byte("\n"))
	if err := cb.ContextFormat(tmpl, cbc.CommonBackupInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	cb.PostFormat(tmpl, NewCommonBackupInfoContext())
	cb.Output.Write([]byte("\n"))

	tmpl, err = cb.startSubsection(commonBackupInfoDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := cb.ContextFormat(tmpl, cbc.CommonBackupInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	cb.PostFormat(tmpl, NewCommonBackupInfoContext())
	cb.Output.Write([]byte("\n"))

	tmpl, err = cb.startSubsection(commonBackupInfoDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := cb.ContextFormat(tmpl, cbc.CommonBackupInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	cb.PostFormat(tmpl, NewCommonBackupInfoContext())
	cb.Output.Write([]byte("\n"))

	// Section 2: Keyspace details subSection 1
	logrus.Debugf("Number of keyspaces: %d", len(cbc.CommonBackupInfo.c.GetResponseList()))
	cb.subSection("Keyspace Details")
	for i, v := range cbc.CommonBackupInfo.c.GetResponseList() {
		keyspaceLocationContext := *NewKeyspaceLocationContext()
		keyspaceLocationContext.Output = os.Stdout
		keyspaceLocationContext.Format = NewCommonBackupInfoFormat(viper.GetString("output"))
		keyspaceLocationContext.SetKeyspaceLocation(v)
		keyspaceLocationContext.Write(i)
		cb.Output.Write([]byte("\n"))
	}

	return nil
}

func (cb *CommonBackupInfoContext) startSubsection(format string) (*template.Template, error) {
	cb.Buffer = bytes.NewBufferString("")
	cb.ContextHeader = ""
	cb.Format = formatter.Format(format)
	cb.PreFormat()

	return cb.ParseFormat()
}

func (cb *CommonBackupInfoContext) subSection(name string) {
	cb.Output.Write([]byte("\n\n"))
	cb.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	cb.Output.Write([]byte("\n"))
}

// NewCommonBackupInfoContext creates a new context for rendering backup
func NewCommonBackupInfoContext() *CommonBackupInfoContext {
	commonBackupInfoCtx := CommonBackupInfoContext{}
	commonBackupInfoCtx.Header = formatter.SubHeaderContext{
		"BackupUUID":             backupUUIDHeader,
		"State":                  stateHeader,
		"StorageConfig":          storageConfigHeader,
		"TableByTableBackup":     tableByTableBackupHeader,
		"TotalBackupSizeInBytes": totalBackupSizeInBytesHeader,
		"CreateTime":             createTimeHeader,
		"UpdateTime":             backupTypeHeader,
		"CompletionTime":         completionTimeHeader,
	}
	return &commonBackupInfoCtx
}

// BackupUUID fetches Backup UUID
func (cb *CommonBackupInfoContext) BackupUUID() string {
	return cb.c.GetBackupUUID()
}

// State fetches Backup State
func (cb *CommonBackupInfoContext) State() string {
	state := cb.c.GetState()
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
func (cb *CommonBackupInfoContext) StorageConfig() string {
	for _, config := range StorageConfigs {
		if strings.Compare(config.GetConfigUUID(), cb.c.GetStorageConfigUUID()) == 0 {
			return fmt.Sprintf("%s(%s)", config.GetConfigName(), config.GetConfigUUID())
		}
	}
	return cb.c.GetStorageConfigUUID()
}

// TableByTableBackup fetches whether Backup is TableByTableBackup
func (cb *CommonBackupInfoContext) TableByTableBackup() bool {
	return cb.c.GetTableByTableBackup()
}

// TotalBackupSizeInBytes fetches whether Backup TotalBackupSizeInBytes
func (cb *CommonBackupInfoContext) TotalBackupSizeInBytes() int64 {
	return cb.c.GetTotalBackupSizeInBytes()
}

// CreateTime fetches whether Backup CreateTime
func (cb *CommonBackupInfoContext) CreateTime() string {
	return cb.c.GetCreateTime().Format(time.RFC1123Z)
}

// UpdateTime fetches whether Backup UpdateTime
func (cb *CommonBackupInfoContext) UpdateTime() string {
	updateTime := cb.c.GetUpdateTime()
	if updateTime.IsZero() {
		return ""
	} else {
		return updateTime.Format(time.RFC1123Z)
	}
}

// CompletionTime fetches whether Backup CompletionTime
func (cb *CommonBackupInfoContext) CompletionTime() string {
	completionTime := cb.c.GetCompletionTime()
	if completionTime.IsZero() {
		return ""
	} else {
		return completionTime.Format(time.RFC1123Z)
	}
}

// MarshalJSON function
func (cb *CommonBackupInfoContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(cb.c)
}
