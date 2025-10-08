/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundle

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const ()

// FullSupportBundleContext to render SupportBundle Details output
type FullSupportBundleContext struct {
	formatter.HeaderContext
	formatter.Context
	sb ybaclient.SupportBundle
}

// SetFullSupportBundle initializes the context with the universe data
func (fsb *FullSupportBundleContext) SetFullSupportBundle(universe ybaclient.SupportBundle) {
	fsb.sb = universe
}

// NewFullSupportBundleFormat for formatting output
func NewFullSupportBundleFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultSupportBundleListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullSupportBundleContext struct {
	SupportBundle *Context
}

// Write populates the output table to be displayed in the command line
func (fsb *FullSupportBundleContext) Write() error {
	var err error
	fsbc := &fullSupportBundleContext{
		SupportBundle: &Context{},
	}
	fsbc.SupportBundle.s = fsb.sb

	// Section 1
	tmpl, err := fsb.startSubsection(defaultSupportBundleListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fsb.Output.Write([]byte("\n"))
	if err := fsb.ContextFormat(tmpl, fsbc.SupportBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.PostFormat(tmpl, NewSupportBundleContext())
	fsb.Output.Write([]byte("\n"))

	// Provider information
	tmpl, err = fsb.startSubsection(supportBundle1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fsb.ContextFormat(tmpl, fsbc.SupportBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.PostFormat(tmpl, NewSupportBundleContext())
	fsb.Output.Write([]byte("\n"))

	// Section 2: SupportBundle Details subSection 1
	tmpl, err = fsb.startSubsection(supportBundle2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fsb.ContextFormat(tmpl, fsbc.SupportBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.PostFormat(tmpl, NewSupportBundleContext())
	fsb.Output.Write([]byte("\n"))

	tmpl, err = fsb.startSubsection(details1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.subSection("Support Bundle Details")
	if err := fsb.ContextFormat(tmpl, fsbc.SupportBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.PostFormat(tmpl, NewSupportBundleContext())
	fsb.Output.Write([]byte("\n"))

	tmpl, err = fsb.startSubsection(details2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fsb.ContextFormat(tmpl, fsbc.SupportBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.PostFormat(tmpl, NewSupportBundleContext())
	fsb.Output.Write([]byte("\n"))

	// Section 3: encryption section
	tmpl, err = fsb.startSubsection(details3)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fsb.ContextFormat(tmpl, fsbc.SupportBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.PostFormat(tmpl, NewSupportBundleContext())
	fsb.Output.Write([]byte("\n"))

	tmpl, err = fsb.startSubsection(details4)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fsb.ContextFormat(tmpl, fsbc.SupportBundle); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fsb.PostFormat(tmpl, NewSupportBundleContext())
	fsb.Output.Write([]byte("\n"))

	return nil
}

func (fsb *FullSupportBundleContext) startSubsection(format string) (*template.Template, error) {
	fsb.Buffer = bytes.NewBufferString("")
	fsb.ContextHeader = ""
	fsb.Format = formatter.Format(format)
	fsb.PreFormat()

	return fsb.ParseFormat()
}

func (fsb *FullSupportBundleContext) subSection(name string) {
	fsb.Output.Write([]byte("\n"))
	fsb.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fsb.Output.Write([]byte("\n"))
}

// NewFullSupportBundleContext creates a new context for rendering universe
func NewFullSupportBundleContext() *FullSupportBundleContext {
	universeCtx := FullSupportBundleContext{}
	universeCtx.Header = formatter.SubHeaderContext{}
	return &universeCtx
}

// MarshalJSON function
func (fsb *FullSupportBundleContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fsb.sb)
}
