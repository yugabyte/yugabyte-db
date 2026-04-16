/*
 * Copyright (c) YugabyteDB, Inc.
 */

package eitdownloadclient

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	downloadEIT     = "table {{.YugabytedbCrt}}"
	downloadKey     = "table {{.YugabytedbKey}}"
	clientCrtHeader = "Client Certificate (yugabytedb.crt)"
	clientKeyHeader = "Client Key (yugabytedb.key)"
)

// Context for eit config outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	eitDownloadClient ybaclient.CertificateDetails
}

// NewEITDownloadClientFormat for formatting output
func NewEITDownloadClientFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := downloadEIT
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of EITs
func Write(ctx formatter.Context, eitDownloadClients []ybaclient.CertificateDetails) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of eits into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(eitDownloadClients, "", "  ")
		} else {
			output, err = json.Marshal(eitDownloadClients)
		}

		if err != nil {
			logrus.Errorf("Error marshaling EIT configs to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, eitDownloadClient := range eitDownloadClients {
			err := format(&Context{eitDownloadClient: eitDownloadClient})
			if err != nil {
				logrus.Debugf("Error rendering eit config: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewEITDownloadClientContext(), render)
}

// NewEITDownloadClientContext creates a new context for rendering eit
func NewEITDownloadClientContext() *Context {
	eitCtx := Context{}
	eitCtx.Header = formatter.SubHeaderContext{
		"YugabytedbCrt": clientCrtHeader,
		"YugabytedbKey": clientKeyHeader,
	}
	return &eitCtx
}

// YugabytedbCrt function
func (c *Context) YugabytedbCrt() string {
	return c.eitDownloadClient.GetYugabytedbCrt()
}

// YugabytedbKey function
func (c *Context) YugabytedbKey() string {
	return c.eitDownloadClient.GetYugabytedbKey()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.eitDownloadClient)
}
