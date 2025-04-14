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
	defaultTablespaceListing = "table {{.TablespaceName}}\t{{.NumReplicas}}\t{{.PlacementBlocks}}"

	tablespaceList1 = "table {{.TablespaceName}}\t{{.NumReplicas}}"

	tablespaceNameHeader  = "Tablespace Name"
	numReplicasHeader     = "Number of Replicas"
	placementBlocksHeader = "Placement Blocks"
)

// TablespaceContext for table outputs
type TablespaceContext struct {
	formatter.HeaderContext
	formatter.Context
	t ybaclient.TableSpaceInfo
}

// NewTablespaceFormat for formatting output
func NewTablespaceFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTablespaceListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// TablespaceWrite renders the context for a list of Table
func TablespaceWrite(ctx formatter.Context, tables []ybaclient.TableSpaceInfo) error {
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
			logrus.Errorf("Error marshaling universe table tablespaces to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, table := range tables {
			err := format(&TablespaceContext{t: table})
			if err != nil {
				logrus.Debugf("Error rendering table: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewTablespaceContext(), render)
}

// NewTablespaceContext creates a new context for rendering table
func NewTablespaceContext() *Context {
	tableCtx := Context{}
	tableCtx.Header = formatter.SubHeaderContext{
		"TablespaceName":  tablespaceNameHeader,
		"NumReplicas":     numReplicasHeader,
		"PlacementBlocks": placementBlocksHeader,
	}
	return &tableCtx
}

// TablespaceName function
func (c *TablespaceContext) TablespaceName() string {
	return c.t.GetName()
}

// NumReplicas function
func (c *TablespaceContext) NumReplicas() string {
	return fmt.Sprintf("%d", c.t.GetNumReplicas())
}

// PlacementBlocks function
func (c *TablespaceContext) PlacementBlocks() string {
	placementBlocks := "-"
	for i, placementBlock := range c.t.GetPlacementBlocks() {
		if i == 0 {
			placementBlocks = placementBlock.GetRegion()
		} else {
			placementBlocks = fmt.Sprintf("%s, %s", placementBlocks, placementBlock.GetRegion())
		}
	}

	return placementBlocks
}

// MarshalJSON function
func (c *TablespaceContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.t)
}
