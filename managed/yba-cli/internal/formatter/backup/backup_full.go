/*
* Copyright (c) YugaByte, Inc.
 */

package backup

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// Backup details
	defaultFullBackupGeneral = "table {{.BackupUUID}}\t{{.BackupType}}\t{{.Category}}\t{{.State}}"
	backupDetails1           = "table {{.Universe}}\t{{.ScheduleName}}" +
		"\t{{.HasIncrementalBackups}}"
	backupDetails2 = "table {{.StorageConfiguration}}\t{{.StorageConfigurationType}}\t{{.KMSConfig}}"
	backupDetails3 = "table {{.CreateTime}}\t{{.CompletionTime}}\t{{.ExpiryTime}}"
)

// StorageConfigs hold storage config for the backup
var StorageConfigs []ybaclient.CustomerConfigUI

// KMSConfigs hold kms configs declared under the current customer
var KMSConfigs []util.KMSConfig

// FullBackupContext to render backup Details output
type FullBackupContext struct {
	formatter.HeaderContext
	formatter.Context
	b ybaclient.BackupResp
}

// SetFullBackup initializes the context with the backup data
func (fb *FullBackupContext) SetFullBackup(backup ybaclient.BackupResp) {
	fb.b = backup
}

// NewFullBackupFormat for formatting output
func NewFullBackupFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultBackupListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullBackupContext struct {
	Backup *Context
}

// Write populates the output table to be displayed in the command line
func (fb *FullBackupContext) Write() error {
	var err error
	fbc := &fullBackupContext{
		Backup: &Context{},
	}
	fbc.Backup.b = fb.b

	// Section 1
	tmpl, err := fb.startSubsection(defaultFullBackupGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fb.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fb.Output.Write([]byte("\n"))
	if err := fb.ContextFormat(tmpl, fbc.Backup); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fb.PostFormat(tmpl, NewBackupContext())
	fb.Output.Write([]byte("\n"))

	// Backup information
	tmpl, err = fb.startSubsection(backupDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fb.ContextFormat(tmpl, fbc.Backup); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fb.PostFormat(tmpl, NewBackupContext())
	fb.Output.Write([]byte("\n"))

	tmpl, err = fb.startSubsection(backupDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fb.ContextFormat(tmpl, fbc.Backup); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fb.PostFormat(tmpl, NewBackupContext())
	fb.Output.Write([]byte("\n"))

	tmpl, err = fb.startSubsection(backupDetails3)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fb.ContextFormat(tmpl, fbc.Backup); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fb.PostFormat(tmpl, NewBackupContext())
	fb.Output.Write([]byte("\n"))

	commonBackupInfo := fbc.Backup.b.GetCommonBackupInfo()
	// Section 2: Keyspace details subSection 1
	logrus.Debugf("Number of keyspaces: %d", len(commonBackupInfo.GetResponseList()))
	fb.subSection("Keyspace Details")
	for i, v := range commonBackupInfo.GetResponseList() {
		keyspaceLocationContext := *NewKeyspaceLocationContext()
		keyspaceLocationContext.Output = os.Stdout
		keyspaceLocationContext.Format = NewFullBackupFormat(viper.GetString("output"))
		keyspaceLocationContext.SetKeyspaceLocation(v)
		keyspaceLocationContext.Write(i)
		fb.Output.Write([]byte("\n"))
	}

	return nil
}

func (fb *FullBackupContext) startSubsection(format string) (*template.Template, error) {
	fb.Buffer = bytes.NewBufferString("")
	fb.ContextHeader = ""
	fb.Format = formatter.Format(format)
	fb.PreFormat()

	return fb.ParseFormat()
}

func (fb *FullBackupContext) subSection(name string) {
	fb.Output.Write([]byte("\n\n"))
	fb.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fb.Output.Write([]byte("\n"))
}

// NewFullBackupContext creates a new context for rendering backup
func NewFullBackupContext() *FullBackupContext {
	backupCtx := FullBackupContext{}
	backupCtx.Header = formatter.SubHeaderContext{}
	return &backupCtx
}

// MarshalJSON function
func (fb *FullBackupContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fb.b)
}
