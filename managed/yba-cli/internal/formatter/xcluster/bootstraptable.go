/*
 * Copyright (c) YugabyteDB, Inc.
 */

package xcluster

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultFullCopyTableListing = "table {{.TableUUID}}\t{{.IsBootstrapRequired}}\t{{.Reasons}}"

	tableUUIDHeader   = "Table UUID"
	isBootstrapHeader = "Is Bootstrap Required"
	reasonsHeader     = "Reasons"
)

// FullCopyTable for table outputs
type FullCopyTable struct {
	formatter.HeaderContext
	formatter.Context
	b util.XClusterConfigNeedBootstrapResponse
}

// NewFullCopyTableFormat for formatting output
func NewFullCopyTableFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultFullCopyTableListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// FullCopyTableWrite renders the context for a list of FullCopyTable
func FullCopyTableWrite(
	ctx formatter.Context,
	tables []util.XClusterConfigNeedBootstrapResponse,
) error {
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
			err := format(&FullCopyTable{b: table})
			if err != nil {
				logrus.Debugf("Error rendering table: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewFullCopyTableContext(), render)
}

// NewFullCopyTableContext creates a new context for rendering table
func NewFullCopyTableContext() *FullCopyTable {
	instanceTypeCtx := FullCopyTable{}
	instanceTypeCtx.Header = formatter.SubHeaderContext{
		"TableUUID":           tableUUIDHeader,
		"IsBootstrapRequired": isBootstrapHeader,
		"Reasons":             reasonsHeader,
	}
	return &instanceTypeCtx
}

// TableUUID of the table
func (c *FullCopyTable) TableUUID() string {
	return c.b.TableUUID
}

// IsBootstrapRequired of the table
func (c *FullCopyTable) IsBootstrapRequired() string {
	return fmt.Sprintf("%t", c.b.BootstrapInfo.IsBootstrapRequired)
}

// Reasons of the table
func (c *FullCopyTable) Reasons() string {
	reasons := ""
	for i, reason := range c.b.BootstrapInfo.Reasons {
		if i == 0 {
			reasons = fmt.Sprintf("%s", reason)
		} else {
			reasons = fmt.Sprintf("%s, %s", reasons, reason)
		}
	}
	return reasons
}

// MarshalJSON function
func (c *FullCopyTable) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.b)
}
