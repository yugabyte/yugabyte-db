/*
 * Copyright (c) YugaByte, Inc.
 */

package table

import (
	"bytes"
	"encoding/json"
	"fmt"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (

	// Column Details
	defaultColumn = "table {{.Name}}\t{{.Type}}\t{{.KeyType}}\t{{.ValueType}}\t{{.ColumnOrder}}"
	columnDetail1 = "table {{.IsClusteringKey}}\t{{.SortOrder}}\t{{.IsPartitionKey}}"

	isClusteringKeyHeader = "Is Clustering Key"
	sortOrderHeader       = "Sort Order"
	isPartitionKeyHeader  = "Is Partition Key"
	valueTypeHeader       = "Value Type"
	columnOrderHeader     = "Relative position for this column, in the table and in CQL commands"
	keyTypeHeader         = "Key Type"
)

// ColumnContext for column outputs
type ColumnContext struct {
	formatter.HeaderContext
	formatter.Context
	c ybaclient.ColumnDetails
}

// NewColumnFormat for formatting output
func NewColumnFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultColumn
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetColumn initializes the context with the column data
func (c *ColumnContext) SetColumn(column ybaclient.ColumnDetails) {
	c.c = column
}

type columnContext struct {
	Column *ColumnContext
}

// Write populates the output table to be displayed in the command line
func (c *ColumnContext) Write(index int) error {
	var err error
	cc := &columnContext{
		Column: &ColumnContext{},
	}
	cc.Column.c = c.c

	// Section 1
	tmpl, err := c.startSubsection(defaultColumn)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Column %d: Details", index+1), formatter.BlueColor)))
	c.Output.Write([]byte("\n"))
	if err := c.ContextFormat(tmpl, cc.Column); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.PostFormat(tmpl, NewColumnContext())

	tmpl, err = c.startSubsection(columnDetail1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.Output.Write([]byte("\n"))
	if err := c.ContextFormat(tmpl, cc.Column); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.PostFormat(tmpl, NewColumnContext())

	return nil
}

func (c *ColumnContext) startSubsection(format string) (*template.Template, error) {
	c.Buffer = bytes.NewBufferString("")
	c.ContextHeader = ""
	c.Format = formatter.Format(format)
	c.PreFormat()

	return c.ParseFormat()
}

func (c *ColumnContext) subSection(name string) {
	c.Output.Write([]byte("\n"))
	c.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	c.Output.Write([]byte("\n"))
}

// NewColumnContext creates a new context for rendering columns
func NewColumnContext() *ColumnContext {
	columnCtx := ColumnContext{}
	columnCtx.Header = formatter.SubHeaderContext{
		"Name":            formatter.NameHeader,
		"Type":            formatter.TypeHeader,
		"KeyType":         keyTypeHeader,
		"ValueType":       valueTypeHeader,
		"ColumnOrder":     columnOrderHeader,
		"IsClusteringKey": isClusteringKeyHeader,
		"SortOrder":       sortOrderHeader,
		"IsPartitionKey":  isPartitionKeyHeader,
	}
	return &columnCtx
}

// Name function
func (c *ColumnContext) Name() string {
	return c.c.GetName()
}

// Type function
func (c *ColumnContext) Type() string {
	return c.c.GetType()
}

// KeyType function
func (c *ColumnContext) KeyType() string {
	return c.c.GetKeyType()
}

// ValueType function
func (c *ColumnContext) ValueType() string {
	return c.c.GetValueType()
}

// ColumnOrder function
func (c *ColumnContext) ColumnOrder() string {
	return fmt.Sprintf("%d", c.c.GetColumnOrder())
}

// IsClusteringKey function
func (c *ColumnContext) IsClusteringKey() string {
	return fmt.Sprintf("%t", c.c.GetIsClusteringKey())
}

// SortOrder function
func (c *ColumnContext) SortOrder() string {
	return c.c.GetSortOrder()
}

// IsPartitionKey function
func (c *ColumnContext) IsPartitionKey() string {
	return fmt.Sprintf("%t", c.c.GetIsPartitionKey())
}

// MarshalJSON function
func (c *ColumnContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.c)
}
