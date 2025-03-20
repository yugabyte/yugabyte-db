/*
 * Copyright (c) YugaByte, Inc.
 */

package keyinfo

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullKeyInfoContext to render KeyInfo Details output
type FullKeyInfoContext struct {
	formatter.HeaderContext
	formatter.Context
	ki ybaclient.ConfKeyInfo
}

// SetFullKeyInfo initializes the context with the provider data
func (fki *FullKeyInfoContext) SetFullKeyInfo(provider ybaclient.ConfKeyInfo) {
	fki.ki = provider
}

// NewFullKeyInfoFormat for formatting output
func NewFullKeyInfoFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultKeyInfoListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullKeyInfoContext struct {
	KeyInfo *Context
}

// Write populates the output table to be displayed in the command line
func (fki *FullKeyInfoContext) Write() error {
	var err error
	fpc := &fullKeyInfoContext{
		KeyInfo: &Context{},
	}
	fpc.KeyInfo.ki = fki.ki

	// Section 1
	tmpl, err := fki.startSubsection(defaultKeyInfoListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fki.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fki.Output.Write([]byte("\n"))
	if err := fki.ContextFormat(tmpl, fpc.KeyInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fki.PostFormat(tmpl, NewKeyInfoContext())
	fki.Output.Write([]byte("\n"))

	// Section 2: KeyInfo Details subSection 1
	tmpl, err = fki.startSubsection(keyInfo1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fki.subSection("Key Info Details")
	if err := fki.ContextFormat(tmpl, fpc.KeyInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fki.PostFormat(tmpl, NewKeyInfoContext())
	fki.Output.Write([]byte("\n"))

	return nil
}

func (fki *FullKeyInfoContext) startSubsection(format string) (*template.Template, error) {
	fki.Buffer = bytes.NewBufferString("")
	fki.ContextHeader = ""
	fki.Format = formatter.Format(format)
	fki.PreFormat()

	return fki.ParseFormat()
}

func (fki *FullKeyInfoContext) subSection(name string) {
	fki.Output.Write([]byte("\n"))
	fki.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fki.Output.Write([]byte("\n"))
}

// NewFullKeyInfoContext creates a new context for rendering provider
func NewFullKeyInfoContext() *FullKeyInfoContext {
	providerCtx := FullKeyInfoContext{}
	providerCtx.Header = formatter.SubHeaderContext{}
	return &providerCtx
}

// MarshalJSON function
func (fki *FullKeyInfoContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fki.ki)
}
