/*
 * Copyright (c) YugaByte, Inc.
 */

package table

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultNamespaceListing = "table {{.NamespaceName}}\t{{.NamespaceUUID}}\t{{.TableType}}"

	namespaceNameHeader = "Namespace Name"
	namespaceUUIDHeader = "Namespace UUID"
)

// NamespaceContext for table outputs
type NamespaceContext struct {
	formatter.HeaderContext
	formatter.Context
	n ybaclient.NamespaceInfoResp
}

// NewNamespaceFormat for formatting output
func NewNamespaceFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultNamespaceListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NamespaceWrite renders the context for a list of Table
func NamespaceWrite(ctx formatter.Context, tables []ybaclient.NamespaceInfoResp) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of universe tables into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(tables, "", "  ")
		} else {
			output, err = json.Marshal(tables)
		}

		if err != nil {
			logrus.Errorf("Error marshaling universe table namespaces to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, table := range tables {
			err := format(&NamespaceContext{n: table})
			if err != nil {
				logrus.Debugf("Error rendering table: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewNamespaceContext(), render)
}

// NewNamespaceContext creates a new context for rendering table
func NewNamespaceContext() *Context {
	tableCtx := Context{}
	tableCtx.Header = formatter.SubHeaderContext{
		"NamespaceUUID": namespaceUUIDHeader,
		"TableType":     tableTypeHeader,
		"NamespaceName": namespaceNameHeader,
	}
	return &tableCtx
}

// NamespaceUUID function
func (c *NamespaceContext) NamespaceUUID() string {
	return c.n.GetNamespaceUUID()
}

// TableType function
func (c *NamespaceContext) TableType() string {
	return c.n.GetTableType()
}

// NamespaceName function
func (c *NamespaceContext) NamespaceName() string {
	return c.n.GetName()
}

// MarshalJSON function
func (c *NamespaceContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.n)
}
