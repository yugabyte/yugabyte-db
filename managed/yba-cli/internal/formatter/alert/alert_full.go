/*
 * Copyright (c) YugabyteDB, Inc.
 */

package alert

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullAlertContext to render Alert Listing output
type FullAlertContext struct {
	formatter.HeaderContext
	formatter.Context
	a ybaclient.Alert
}

// SetFullAlert initializes the context with the alert data
func (fa *FullAlertContext) SetFullAlert(alert ybaclient.Alert) {
	fa.a = alert
}

// NewFullAlertFormat for formatting output
func NewFullAlertFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullAlertContext struct {
	Alert *Context
}

// Write populates the output table to be displayed in the command line
func (fa *FullAlertContext) Write() error {
	var err error
	fpc := &fullAlertContext{
		Alert: &Context{},
	}
	fpc.Alert.a = fa.a

	// Section 1
	tmpl, err := fa.startSubsection(defaultAlertListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fa.Output.Write([]byte("\n"))
	if err := fa.ContextFormat(tmpl, fpc.Alert); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertContext())
	fa.Output.Write([]byte("\n"))

	// Section 2: Alert Listing subSection 1
	tmpl, err = fa.startSubsection(alertListing1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.Alert); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertContext())
	fa.Output.Write([]byte("\n"))

	// Section 2: Alert Listing subSection 2

	tmpl, err = fa.startSubsection(alertListing3)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.Alert); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertContext())
	fa.Output.Write([]byte("\n"))

	tmpl, err = fa.startSubsection(alertListing4)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.Alert); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertContext())
	fa.Output.Write([]byte("\n"))

	tmpl, err = fa.startSubsection(alertListing5)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.Alert); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertContext())
	fa.Output.Write([]byte("\n"))

	return nil
}

func (fa *FullAlertContext) startSubsection(format string) (*template.Template, error) {
	fa.Buffer = bytes.NewBufferString("")
	fa.ContextHeader = ""
	fa.Format = formatter.Format(format)
	fa.PreFormat()

	return fa.ParseFormat()
}

// NewFullAlertContext creates a new context for rendering alert
func NewFullAlertContext() *FullAlertContext {
	alertCtx := FullAlertContext{}
	alertCtx.Header = formatter.SubHeaderContext{}
	return &alertCtx
}

// MarshalJSON function
func (fa *FullAlertContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fa.a)
}
