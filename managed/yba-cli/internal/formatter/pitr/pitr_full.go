/*
 * Copyright (c) YugaByte, Inc.
 */

package pitr

import (
	"bytes"
	"encoding/json"
	"fmt"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullPITRContext to render PITR Details output
type FullPITRContext struct {
	formatter.HeaderContext
	formatter.Context
	p ybaclient.PitrConfig
}

// SetFullPITR initializes the context with the pitr data
func (fp *FullPITRContext) SetFullPITR(pitr ybaclient.PitrConfig) {
	fp.p = pitr
}

// NewFullPITRFormat for formatting output
func NewFullPITRFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultPITRListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullPITRContext struct {
	PITR *Context
}

// Write populates the output table to be displayed in the command line
func (fp *FullPITRContext) Write(index int) error {
	var err error
	fxc := &fullPITRContext{
		PITR: &Context{},
	}
	fxc.PITR.p = fp.p

	fp.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("PITR Configuration %d: Details", index+1), formatter.BlueColor)))
	fp.Output.Write([]byte("\n"))

	// Section 1
	tmpl, err := fp.startSubsection(defaultPITRListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.Output.Write([]byte("\n"))
	if err := fp.ContextFormat(tmpl, fxc.PITR); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewPITRContext())
	fp.Output.Write([]byte("\n"))

	// Section 2: PITR Details subSection 1
	tmpl, err = fp.startSubsection(pitrListing1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.subSection("PITR Details")
	if err := fp.ContextFormat(tmpl, fxc.PITR); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewPITRContext())
	fp.Output.Write([]byte("\n"))

	// Section 2: PITR Details subSection 2
	tmpl, err = fp.startSubsection(pitrListing2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fp.ContextFormat(tmpl, fxc.PITR); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewPITRContext())
	fp.Output.Write([]byte("\n"))

	tmpl, err = fp.startSubsection(pitrListing3)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fp.ContextFormat(tmpl, fxc.PITR); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewPITRContext())
	fp.Output.Write([]byte("\n"))

	return nil
}

func (fp *FullPITRContext) startSubsection(format string) (*template.Template, error) {
	fp.Buffer = bytes.NewBufferString("")
	fp.ContextHeader = ""
	fp.Format = formatter.Format(format)
	fp.PreFormat()

	return fp.ParseFormat()
}

func (fp *FullPITRContext) subSection(name string) {
	fp.Output.Write([]byte("\n\n"))
	fp.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	fp.Output.Write([]byte("\n"))
}

// NewFullPITRContext creates a new context for rendering pitr
func NewFullPITRContext() *FullPITRContext {
	pitrCtx := FullPITRContext{}
	pitrCtx.Header = formatter.SubHeaderContext{}
	return &pitrCtx
}

// MarshalJSON function
func (fp *FullPITRContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fp.p)
}
