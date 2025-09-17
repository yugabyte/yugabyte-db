/*
* Copyright (c) YugabyteDB, Inc.
 */

package commonbackupinfo

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup"
)

// FullCommonBackupInfoContext to render CommonBackupInfo Details output
type FullCommonBackupInfoContext struct {
	formatter.HeaderContext
	formatter.Context
	cb ybaclient.CommonBackupInfo
}

// SetFullCommonBackupInfo initializes the context with the commonBackupInfo data
func (fcb *FullCommonBackupInfoContext) SetFullCommonBackupInfo(
	commonBackupInfo ybaclient.CommonBackupInfo,
) {
	fcb.cb = commonBackupInfo
}

// NewFullCommonBackupInfoFormat for formatting output
func NewFullCommonBackupInfoFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultCommonBackupInfoListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullCommonBackupInfoContext struct {
	CommonBackupInfo *Context
}

// Write populates the output table to be displayed in the command line
func (fcb *FullCommonBackupInfoContext) Write(index int) error {
	var err error
	fcbc := &fullCommonBackupInfoContext{
		CommonBackupInfo: &Context{},
	}
	fcbc.CommonBackupInfo.c = fcb.cb

	// Section 1
	tmpl, err := fcb.startSubsection(defaultCommonBackupInfoListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fcb.Output.Write([]byte(
		formatter.Colorize(
			fmt.Sprintf("Common Backup %d Details", index+1),
			formatter.BlueColor)))
	fcb.Output.Write([]byte("\n"))
	if err := fcb.ContextFormat(tmpl, fcbc.CommonBackupInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fcb.PostFormat(tmpl, NewCommonBackupInfoContext())
	fcb.Output.Write([]byte("\n"))

	tmpl, err = fcb.startSubsection(commonBackupInfoDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fcb.ContextFormat(tmpl, fcbc.CommonBackupInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fcb.PostFormat(tmpl, NewCommonBackupInfoContext())
	fcb.Output.Write([]byte("\n"))

	tmpl, err = fcb.startSubsection(commonBackupInfoDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fcb.ContextFormat(tmpl, fcbc.CommonBackupInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fcb.PostFormat(tmpl, NewCommonBackupInfoContext())
	fcb.Output.Write([]byte("\n"))

	// Section 2: Keyspace details subSection 1
	logrus.Debugf("Number of keyspaces: %d", len(fcbc.CommonBackupInfo.c.GetResponseList()))
	fcb.subSection("Keyspace Details")
	for i, v := range fcbc.CommonBackupInfo.c.GetResponseList() {
		keyspaceLocationContext := *backup.NewKeyspaceLocationContext()
		keyspaceLocationContext.Output = os.Stdout
		keyspaceLocationContext.Format = NewCommonBackupInfoFormat(viper.GetString("output"))
		keyspaceLocationContext.SetKeyspaceLocation(v)
		keyspaceLocationContext.Write(i)
		fcb.Output.Write([]byte("\n"))
	}

	return nil
}

func (fcb *FullCommonBackupInfoContext) startSubsection(format string) (*template.Template, error) {
	fcb.Buffer = bytes.NewBufferString("")
	fcb.ContextHeader = ""
	fcb.Format = formatter.Format(format)
	fcb.PreFormat()

	return fcb.ParseFormat()
}

func (fcb *FullCommonBackupInfoContext) subSection(name string) {
	fcb.Output.Write([]byte("\n"))
	fcb.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fcb.Output.Write([]byte("\n"))
}

// NewFullCommonBackupInfoContext creates a new context for rendering commonBackupInfo
func NewFullCommonBackupInfoContext() *FullCommonBackupInfoContext {
	providerCtx := FullCommonBackupInfoContext{}
	providerCtx.Header = formatter.SubHeaderContext{}
	return &providerCtx
}

// MarshalJSON function
func (fcb *FullCommonBackupInfoContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fcb.cb)
}
