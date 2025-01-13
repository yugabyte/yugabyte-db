/*
 * Copyright (c) YugaByte, Inc.
 */

package table

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultTableDefinitionListing = "table {{.TableName}}\t{{.TableUUID}}" +
		"\t{{.TableType}}\t{{.Keyspace}}"

	tableDetails1 = "table {{.TTLInSeconds}}"
	tableDetails2 = "table {{.SplitValues}}"

	ttlInSecondsHeader = "TTL (in seconds, -1 represents infinite TTL)"
	splitValuesHeader  = "Split Values (Primary key values used to split table into tablets)"
	columnsHeader      = "Columns"
)

// DefinitionContext for table outputs
type DefinitionContext struct {
	formatter.HeaderContext
	formatter.Context
	t ybaclient.TableDefinitionTaskParams
}

// NewDefinitionFormat for formatting output
func NewDefinitionFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTableDefinitionListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// DefinitionWrite renders the context for a list of Table
func DefinitionWrite(ctx formatter.Context, tables []ybaclient.TableDefinitionTaskParams) error {
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
			logrus.Errorf("Error marshaling universe tables to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, table := range tables {
			err := format(&DefinitionContext{t: table})
			if err != nil {
				logrus.Debugf("Error rendering table: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewDefinitionContext(), render)
}

// NewDefinitionContext creates a new context for rendering table
func NewDefinitionContext() *Context {
	tableCtx := Context{}
	tableCtx.Header = formatter.SubHeaderContext{
		"TableUUID":    tableUUIDHeader,
		"TableType":    tableTypeHeader,
		"TableName":    tableNameHeader,
		"Keyspace":     keySpaceHeader,
		"TTLInSeconds": ttlInSecondsHeader,
		"SplitValues":  splitValuesHeader,
		"Columns":      columnsHeader,
	}
	return &tableCtx
}

// TableUUID function
func (c *DefinitionContext) TableUUID() string {
	return c.t.GetTableUUID()
}

// TableType function
func (c *DefinitionContext) TableType() string {
	return c.t.GetTableType()
}

// TableName function
func (c *DefinitionContext) TableName() string {
	details := c.t.GetTableDetails()
	return details.GetTableName()
}

// Keyspace function
func (c *DefinitionContext) Keyspace() string {
	details := c.t.GetTableDetails()
	return details.GetKeyspace()
}

// TTLInSeconds function
func (c *DefinitionContext) TTLInSeconds() string {
	details := c.t.GetTableDetails()
	return fmt.Sprintf("%d", details.GetTtlInSeconds())
}

// SplitValues function
func (c *DefinitionContext) SplitValues() string {
	details := c.t.GetTableDetails()
	splitValues := "-"
	for i, v := range details.GetSplitValues() {
		if i != 0 {
			splitValues = fmt.Sprintf("%s,%s", splitValues, v)
		} else {
			splitValues = v
		}
	}
	return splitValues
}

// Columns function
func (c *DefinitionContext) Columns() string {
	details := c.t.GetTableDetails()
	columns := "-"
	for i, v := range details.GetColumns() {
		if i != 0 {
			columns = fmt.Sprintf("%s,%s", columns, v.GetName())
		} else {
			columns = v.GetName()
		}
	}
	return columns
}

// MarshalJSON function
func (c *DefinitionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.t)
}
