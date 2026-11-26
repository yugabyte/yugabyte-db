/*
 * Copyright (c) YugabyteDB, Inc.
 */

package perfadvisor

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullEndpointContext renders every field of one Perf Advisor endpoint
type FullEndpointContext struct {
	formatter.HeaderContext
	formatter.Context
	pe ybav2client.PerfAdvisorEndpoint
}

// SetFullEndpoint initializes the context with the endpoint data
func (fe *FullEndpointContext) SetFullEndpoint(endpoint ybav2client.PerfAdvisorEndpoint) {
	fe.pe = endpoint
}

// NewFullEndpointFormat for formatting output
func NewFullEndpointFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(defaultEndpointListing)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullEndpointContext struct {
	Endpoint *EndpointContext
}

// Write populates the output table to be displayed in the command line
func (fe *FullEndpointContext) Write() error {
	fec := &fullEndpointContext{Endpoint: &EndpointContext{}}
	fec.Endpoint.pe = fe.pe

	sections := []struct {
		format string
		title  string
	}{
		{defaultEndpointListing, "General"},
		{endpoint1, ""},
		{endpoint2, ""},
		{endpoint3, ""},
		{endpoint4, ""},
	}

	for _, section := range sections {
		tmpl, err := fe.startSubsection(section.format)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if section.title != "" {
			fe.Output.Write([]byte(formatter.Colorize(section.title, formatter.GreenColor)))
		}
		fe.Output.Write([]byte("\n"))
		if err := fe.ContextFormat(tmpl, fec.Endpoint); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fe.PostFormat(tmpl, NewEndpointContext())
	}
	return nil
}

func (fe *FullEndpointContext) startSubsection(format string) (*template.Template, error) {
	fe.Buffer = bytes.NewBufferString("")
	fe.ContextHeader = ""
	fe.Format = formatter.Format(format)
	fe.PreFormat()

	return fe.ParseFormat()
}

// NewFullEndpointContext creates a new context for rendering an endpoint
func NewFullEndpointContext() *FullEndpointContext {
	endpointCtx := FullEndpointContext{}
	endpointCtx.Header = formatter.SubHeaderContext{}
	return &endpointCtx
}

// MarshalJSON renders the endpoint as JSON
func (fe *FullEndpointContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fe.pe)
}
