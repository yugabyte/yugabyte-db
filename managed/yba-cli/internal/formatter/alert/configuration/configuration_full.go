/*
 * Copyright (c) YugabyteDB, Inc.
 */

package configuration

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullAlertConfigurationContext to render AlertConfiguration Listing output
type FullAlertConfigurationContext struct {
	formatter.HeaderContext
	formatter.Context
	a ybaclient.AlertConfiguration
}

// SetFullAlertConfiguration initializes the context with the alert data
func (fa *FullAlertConfigurationContext) SetFullAlertConfiguration(
	alert ybaclient.AlertConfiguration,
) {
	fa.a = alert
}

// NewFullAlertConfigurationFormat for formatting output
func NewFullAlertConfigurationFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertConfigurationListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullAlertConfigurationContext struct {
	AlertConfiguration *Context
}

// Write populates the output table to be displayed in the command line
func (fa *FullAlertConfigurationContext) Write() error {
	var err error
	fpc := &fullAlertConfigurationContext{
		AlertConfiguration: &Context{},
	}
	fpc.AlertConfiguration.a = fa.a

	// Section 1
	tmpl, err := fa.startSubsection(defaultAlertConfigurationListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fa.Output.Write([]byte("\n"))
	if err := fa.ContextFormat(tmpl, fpc.AlertConfiguration); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertConfigurationContext())
	fa.Output.Write([]byte("\n"))

	// Section 2: AlertConfiguration Listing subSection 1
	tmpl, err = fa.startSubsection(alertConfiguration1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.AlertConfiguration); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertConfigurationContext())
	fa.Output.Write([]byte("\n"))

	// Section 2: AlertConfiguration Listing subSection 2

	tmpl, err = fa.startSubsection(alertConfiguration2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.AlertConfiguration); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertConfigurationContext())
	fa.Output.Write([]byte("\n"))

	tmpl, err = fa.startSubsection(alertConfiguration3)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.AlertConfiguration); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertConfigurationContext())
	fa.Output.Write([]byte("\n"))

	tmpl, err = fa.startSubsection(alertConfiguration4)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.AlertConfiguration); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertConfigurationContext())
	fa.Output.Write([]byte("\n"))

	tmpl, err = fa.startSubsection(alertConfiguration5)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.AlertConfiguration); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertConfigurationContext())
	fa.Output.Write([]byte("\n"))

	tmpl, err = fa.startSubsection(alertConfiguration6)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.AlertConfiguration); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertConfigurationContext())
	fa.Output.Write([]byte("\n"))

	tmpl, err = fa.startSubsection(alertConfiguration7)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fa.ContextFormat(tmpl, fpc.AlertConfiguration); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertConfigurationContext())
	fa.Output.Write([]byte("\n"))

	return nil
}

func (fa *FullAlertConfigurationContext) startSubsection(
	format string,
) (*template.Template, error) {
	fa.Buffer = bytes.NewBufferString("")
	fa.ContextHeader = ""
	fa.Format = formatter.Format(format)
	fa.PreFormat()

	return fa.ParseFormat()
}

func (fa *FullAlertConfigurationContext) subSection(name string) {
	fa.Output.Write([]byte("\n"))
	fa.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fa.Output.Write([]byte("\n"))
}

// NewFullAlertConfigurationContext creates a new context for rendering alert
func NewFullAlertConfigurationContext() *FullAlertConfigurationContext {
	alertCtx := FullAlertConfigurationContext{}
	alertCtx.Header = formatter.SubHeaderContext{}
	return &alertCtx
}

// MarshalJSON function
func (fa *FullAlertConfigurationContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fa.a)
}
