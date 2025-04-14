/*
 * Copyright (c) YugaByte, Inc.
 */

package maintenancewindow

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullMaintenanceWindowContext to render MaintenanceWindow Listing output
type FullMaintenanceWindowContext struct {
	formatter.HeaderContext
	formatter.Context
	mw ybaclient.MaintenanceWindow
}

// SetFullMaintenanceWindow initializes the context with the maintenanceWindow data
func (fmw *FullMaintenanceWindowContext) SetFullMaintenanceWindow(
	maintenanceWindow ybaclient.MaintenanceWindow,
) {
	fmw.mw = maintenanceWindow
}

// NewFullMaintenanceWindowFormat for formatting output
func NewFullMaintenanceWindowFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultMaintenanceWindowListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullMaintenanceWindowContext struct {
	MaintenanceWindow *Context
}

// Write populates the output table to be displayed in the command line
func (fmw *FullMaintenanceWindowContext) Write() error {
	var err error
	fmwc := &fullMaintenanceWindowContext{
		MaintenanceWindow: &Context{},
	}
	fmwc.MaintenanceWindow.mw = fmw.mw

	// Section 1
	tmpl, err := fmw.startSubsection(defaultMaintenanceWindowListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fmw.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fmw.Output.Write([]byte("\n"))
	if err := fmw.ContextFormat(tmpl, fmwc.MaintenanceWindow); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fmw.PostFormat(tmpl, NewMaintenanceWindowContext())
	fmw.Output.Write([]byte("\n"))

	// Section 2: MaintenanceWindow Listing subSection 1
	tmpl, err = fmw.startSubsection(maintenanceWindowListing1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fmw.ContextFormat(tmpl, fmwc.MaintenanceWindow); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fmw.PostFormat(tmpl, NewMaintenanceWindowContext())
	fmw.Output.Write([]byte("\n"))
	tmpl, err = fmw.startSubsection(maintenanceWindowListing2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fmw.ContextFormat(tmpl, fmwc.MaintenanceWindow); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fmw.PostFormat(tmpl, NewMaintenanceWindowContext())
	fmw.Output.Write([]byte("\n"))
	tmpl, err = fmw.startSubsection(maintenanceWindowListing3)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fmw.ContextFormat(tmpl, fmwc.MaintenanceWindow); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fmw.PostFormat(tmpl, NewMaintenanceWindowContext())
	fmw.Output.Write([]byte("\n"))
	return nil
}

func (fmw *FullMaintenanceWindowContext) startSubsection(
	format string,
) (*template.Template, error) {
	fmw.Buffer = bytes.NewBufferString("")
	fmw.ContextHeader = ""
	fmw.Format = formatter.Format(format)
	fmw.PreFormat()

	return fmw.ParseFormat()
}

func (fmw *FullMaintenanceWindowContext) subSection(name string) {
	fmw.Output.Write([]byte("\n"))
	fmw.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fmw.Output.Write([]byte("\n"))
}

// NewFullMaintenanceWindowContext creates mw new context for rendering maintenanceWindow
func NewFullMaintenanceWindowContext() *FullMaintenanceWindowContext {
	maintenanceWindowCtx := FullMaintenanceWindowContext{}
	maintenanceWindowCtx.Header = formatter.SubHeaderContext{}
	return &maintenanceWindowCtx
}

// MarshalJSON function
func (fmw *FullMaintenanceWindowContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fmw.mw)
}
