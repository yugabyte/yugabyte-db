/*
 * Copyright (c) YugaByte, Inc.
 */

package destination

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullAlertDestinationContext to render AlertDestination Listing output
type FullAlertDestinationContext struct {
	formatter.HeaderContext
	formatter.Context
	a ybaclient.AlertDestination
}

// SetFullAlertDestination initializes the context with the alert data
func (fa *FullAlertDestinationContext) SetFullAlertDestination(
	alert ybaclient.AlertDestination,
) {
	fa.a = alert
}

// NewFullAlertDestinationFormat for formatting output
func NewFullAlertDestinationFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertDestinationListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullAlertDestinationContext struct {
	AlertDestination *Context
}

// Write populates the output table to be displayed in the command line
func (fa *FullAlertDestinationContext) Write() error {
	var err error
	fpc := &fullAlertDestinationContext{
		AlertDestination: &Context{},
	}
	fpc.AlertDestination.a = fa.a

	// Section 1
	tmpl, err := fa.startSubsection(defaultAlertDestinationListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fa.Output.Write([]byte("\n"))
	if err := fa.ContextFormat(tmpl, fpc.AlertDestination); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertDestinationContext())
	fa.Output.Write([]byte("\n"))

	// Section 2: AlertDestination Listing subSection 1
	tmpl, err = fa.startSubsection(alertDestination1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.AlertDestination); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertDestinationContext())
	fa.Output.Write([]byte("\n"))
	return nil
}

func (fa *FullAlertDestinationContext) startSubsection(
	format string,
) (*template.Template, error) {
	fa.Buffer = bytes.NewBufferString("")
	fa.ContextHeader = ""
	fa.Format = formatter.Format(format)
	fa.PreFormat()

	return fa.ParseFormat()
}

func (fa *FullAlertDestinationContext) subSection(name string) {
	fa.Output.Write([]byte("\n"))
	fa.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fa.Output.Write([]byte("\n"))
}

// NewFullAlertDestinationContext creates a new context for rendering alert
func NewFullAlertDestinationContext() *FullAlertDestinationContext {
	alertCtx := FullAlertDestinationContext{}
	alertCtx.Header = formatter.SubHeaderContext{}
	return &alertCtx
}

// MarshalJSON function
func (fa *FullAlertDestinationContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fa.a)
}
