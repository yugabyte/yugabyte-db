/*
 * Copyright (c) YugabyteDB, Inc.
 */

package eitdownloadroot

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullEITDownloadRootContext to render EITDownloadRoot Details output
type FullEITDownloadRootContext struct {
	formatter.HeaderContext
	formatter.Context
	eitDownloadRoot map[string]interface{}
}

// SetFullEITDownloadRoot initializes the context with the eit data
func (feit *FullEITDownloadRootContext) SetFullEITDownloadRoot(eit map[string]interface{}) {
	feit.eitDownloadRoot = eit
}

// NewFullEITDownloadRootFormat for formatting output
func NewFullEITDownloadRootFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := downloadEIT
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullEITDownloadRootContext struct {
	EITDownloadRoot *Context
}

// Write populates the output table to be displayed in the command line
func (feit *FullEITDownloadRootContext) Write() error {
	var err error
	feitc := &fullEITDownloadRootContext{
		EITDownloadRoot: &Context{},
	}
	feitc.EITDownloadRoot.eitDownloadRoot = feit.eitDownloadRoot

	// Section 1
	tmpl, err := feit.startSubsection(downloadEIT)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.Output.Write([]byte(formatter.Colorize("Certificate Contents", formatter.GreenColor)))
	feit.Output.Write([]byte("\n"))
	if err := feit.ContextFormat(tmpl, feitc.EITDownloadRoot); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.PostFormat(tmpl, NewEITDownloadRootContext())
	feit.Output.Write([]byte("\n"))

	return nil
}

func (feit *FullEITDownloadRootContext) startSubsection(format string) (*template.Template, error) {
	feit.Buffer = bytes.NewBufferString("")
	feit.ContextHeader = ""
	feit.Format = formatter.Format(format)
	feit.PreFormat()

	return feit.ParseFormat()
}

// NewFullEITDownloadRootContext creates a new context for rendering eit
func NewFullEITDownloadRootContext() *FullEITDownloadRootContext {
	eitCtx := FullEITDownloadRootContext{}
	eitCtx.Header = formatter.SubHeaderContext{}
	return &eitCtx
}

// MarshalJSON function
func (feit *FullEITDownloadRootContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(feit.eitDownloadRoot)
}
