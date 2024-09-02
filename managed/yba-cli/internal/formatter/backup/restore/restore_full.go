/*
* Copyright (c) YugaByte, Inc.
 */

package restore

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultFullRestoreGeneral = "table {{.RestoreUUID}}\t{{.BackupType}}" +
		"\t{{.CreateTime}}\t{{.CompletionTime}}\t{{.State}}"

	restoreDetails1 = "table {{.Universe}}\t{{.SourceUniverse}}"
)

// FullRestoreContext to render Provider Details output
type FullRestoreContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.RestoreResp
}

// SetFullRestore initializes the context with the restore data
func (fr *FullRestoreContext) SetFullRestore(restore ybaclient.RestoreResp) {
	fr.r = restore
}

// NewFullRestoreFormat for formatting output
func NewFullRestoreFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultRestoreListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullRestoreContext struct {
	Restore *Context
}

// Write populates the output table to be displayed in the command line
func (fr *FullRestoreContext) Write() error {
	var err error
	frc := &restoreContext{
		Restore: &Context{},
	}
	frc.Restore.r = fr.r

	// Section 1
	tmpl, err := fr.startSubsection(defaultFullRestoreGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
	if err := fr.ContextFormat(tmpl, frc.Restore); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewRestoreContext())
	fr.Output.Write([]byte("\n"))

	// Restore information
	tmpl, err = fr.startSubsection(restoreDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fr.ContextFormat(tmpl, frc.Restore); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewRestoreContext())
	fr.Output.Write([]byte("\n"))

	// Section 2: Keyspace details subSection 1
	logrus.Debugf("Number of keyspaces: %d", len(frc.Restore.r.GetRestoreKeyspaceList()))
	if len(frc.Restore.r.GetRestoreKeyspaceList()) > 0 {
		fr.subSection("Keyspace Details")
		for i, v := range frc.Restore.r.GetRestoreKeyspaceList() {
			restoreKeyspaceContext := *NewRestoreKeyspaceContext()
			restoreKeyspaceContext.Output = os.Stdout
			restoreKeyspaceContext.Format = NewRestoreFormat(viper.GetString("output"))
			restoreKeyspaceContext.SetRestoreKeyspace(v)
			restoreKeyspaceContext.Write(i)
			fr.Output.Write([]byte("\n"))
		}
	}

	return nil
}

func (fr *FullRestoreContext) startSubsection(format string) (*template.Template, error) {
	fr.Buffer = bytes.NewBufferString("")
	fr.ContextHeader = ""
	fr.Format = formatter.Format(format)
	fr.PreFormat()

	return fr.ParseFormat()
}

func (fr *FullRestoreContext) subSection(name string) {
	fr.Output.Write([]byte("\n\n"))
	fr.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
}

// NewFullRestoreContext creates a new context for rendering restore
func NewFullRestoreContext() *FullRestoreContext {
	restoreCtx := FullRestoreContext{}
	restoreCtx.Header = formatter.SubHeaderContext{}
	return &restoreCtx
}

// MarshalJSON function
func (fr *FullRestoreContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fr.r)
}
