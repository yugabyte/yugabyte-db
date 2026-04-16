/*
 * Copyright (c) YugabyteDB, Inc.
 */

package eitdownloadroot

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	downloadEIT   = "table {{.RootCrt}}"
	rootCrtHeader = "Root Certificate (root.crt)"
)

// Context for eit config outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	eitDownloadRoot map[string]interface{}
}

// NewEITDownloadRootFormat for formatting output
func NewEITDownloadRootFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := downloadEIT
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of EITs
func Write(ctx formatter.Context, eitDownloadRoots []map[string]interface{}) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of eits into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(eitDownloadRoots, "", "  ")
		} else {
			output, err = json.Marshal(eitDownloadRoots)
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
		for _, eitDownloadRoot := range eitDownloadRoots {
			err := format(&Context{eitDownloadRoot: eitDownloadRoot})
			if err != nil {
				logrus.Debugf("Error rendering eit config: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewEITDownloadRootContext(), render)
}

// NewEITDownloadRootContext creates a new context for rendering eit
func NewEITDownloadRootContext() *Context {
	eitCtx := Context{}
	eitCtx.Header = formatter.SubHeaderContext{
		"RootCrt": rootCrtHeader,
	}
	return &eitCtx
}

// RootCrt fetches EIT Root Crt
func (c *Context) RootCrt() string {
	return fmt.Sprintf("%v", c.eitDownloadRoot["root.crt"])
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.eitDownloadRoot)
}
