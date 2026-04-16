/*
 * Copyright (c) YugabyteDB, Inc.
 */

package eitdownloadclient

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullEITDownloadClientContext to render EITDownloadClient Details output
type FullEITDownloadClientContext struct {
	formatter.HeaderContext
	formatter.Context
	eitDownloadClient ybaclient.CertificateDetails
}

// SetFullEITDownloadClient initializes the context with the eit data
func (feit *FullEITDownloadClientContext) SetFullEITDownloadClient(
	eit ybaclient.CertificateDetails,
) {
	feit.eitDownloadClient = eit
}

// NewFullEITDownloadClientFormat for formatting output
func NewFullEITDownloadClientFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := downloadEIT
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullEITDownloadClientContext struct {
	EITDownloadClient *Context
}

// Write populates the output table to be displayed in the command line
func (feit *FullEITDownloadClientContext) Write() error {
	var err error
	feitc := &fullEITDownloadClientContext{
		EITDownloadClient: &Context{},
	}
	feitc.EITDownloadClient.eitDownloadClient = feit.eitDownloadClient

	// Section 1
	tmpl, err := feit.startSubsection(downloadEIT)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.Output.Write([]byte(formatter.Colorize("Certificate Contents", formatter.GreenColor)))
	feit.Output.Write([]byte("\n"))
	if err := feit.ContextFormat(tmpl, feitc.EITDownloadClient); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.PostFormat(tmpl, NewEITDownloadClientContext())
	feit.Output.Write([]byte("\n"))

	tmpl, err = feit.startSubsection(downloadKey)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := feit.ContextFormat(tmpl, feitc.EITDownloadClient); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.PostFormat(tmpl, NewEITDownloadClientContext())
	feit.Output.Write([]byte("\n"))
	return nil
}

func (feit *FullEITDownloadClientContext) startSubsection(format string) (
	*template.Template,
	error,
) {
	feit.Buffer = bytes.NewBufferString("")
	feit.ContextHeader = ""
	feit.Format = formatter.Format(format)
	feit.PreFormat()

	return feit.ParseFormat()
}

// NewFullEITDownloadClientContext creates a new context for rendering eit
func NewFullEITDownloadClientContext() *FullEITDownloadClientContext {
	eitCtx := FullEITDownloadClientContext{}
	eitCtx.Header = formatter.SubHeaderContext{}
	return &eitCtx
}

// MarshalJSON function
func (feit *FullEITDownloadClientContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(feit.eitDownloadClient)
}
