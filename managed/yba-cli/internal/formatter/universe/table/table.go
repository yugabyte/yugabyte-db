/*
 * Copyright (c) YugaByte, Inc.
 */

package table

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultTableListing = "table {{.TableName}}\t{{.TableUUID}}" +
		"\t{{.TableType}}\t{{.SizeBytes}}\t{{.WalSizeBytes}}\t{{.KeySpace}}\t{{.PgSchemaName}}"

	tableNameHeader = "Table Name"
	tableUUIDHeader = "Table UUID"

	colocatedHeader          = "Colocated"
	colocationParentIDHeader = "Colocation Parent ID"
	indexTableIDsHeader      = "Index Table IDs"
	keySpaceHeader           = "KeySpace"
	mainTableUUIDHeader      = "Main Table UUID"
	nameSpaceHeader          = "NameSpace"
	parentTableUUIDHeader    = "Parent Table UUID"
	pgSchemaNameHeader       = "PG Schema Name"
	relationTypeHeader       = "Relation Type"
	sizeBytesHeader          = "SST size"
	tableIDHeader            = "Table ID"
	tableSpaceHeader         = "TableSpace"
	tableTypeHeader          = "Table Type"
	walSizeBytesHeader       = "WAL Size"
)

// Context for table outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	t ybaclient.TableInfoResp
}

// NewTableFormat for formatting output
func NewTableFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTableListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Table
func Write(ctx formatter.Context, tables []ybaclient.TableInfoResp) error {
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
			err := format(&Context{t: table})
			if err != nil {
				logrus.Debugf("Error rendering table: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewTableContext(), render)
}

// NewTableContext creates a new context for rendering table
func NewTableContext() *Context {
	tableCtx := Context{}
	tableCtx.Header = formatter.SubHeaderContext{
		"TableName":          tableNameHeader,
		"TableUUID":          tableUUIDHeader,
		"Colocated":          colocatedHeader,
		"ColocationParentID": colocationParentIDHeader,
		"IndexTableIDs":      indexTableIDsHeader,
		"KeySpace":           keySpaceHeader,
		"MainTableUUID":      mainTableUUIDHeader,
		"NameSpace":          nameSpaceHeader,
		"ParentTableUUID":    parentTableUUIDHeader,
		"PgSchemaName":       pgSchemaNameHeader,
		"RelationType":       relationTypeHeader,
		"SizeBytes":          sizeBytesHeader,
		"TableSpace":         tableSpaceHeader,
		"TableType":          tableTypeHeader,
		"WalSizeBytes":       walSizeBytesHeader,
	}
	return &tableCtx
}

// TableName of the table
func (c *Context) TableName() string {
	return c.t.GetTableName()
}

// TableUUID of the table
func (c *Context) TableUUID() string {
	return c.t.GetTableUUID()
}

// Colocated returns true if the table is colocated
func (c *Context) Colocated() string {
	return fmt.Sprintf("%t", c.t.GetColocated())
}

// ColocationParentID returns the parent id of the table
func (c *Context) ColocationParentID() string {
	return c.t.GetColocationParentId()
}

// IndexTableIDs returns the index table ids of the table
func (c *Context) IndexTableIDs() string {
	ids := ""
	for i, id := range c.t.GetIndexTableIDs() {
		if i == 0 {
			ids = id
		} else {
			ids = fmt.Sprintf("%s, %s", ids, id)
		}
	}
	return ids
}

// KeySpace returns the KeySpace of the table
func (c *Context) KeySpace() string {
	return c.t.GetKeySpace()
}

// MainTableUUID returns the main table uuid of the table
func (c *Context) MainTableUUID() string {
	return c.t.GetMainTableUUID()
}

// NameSpace returns the namespace of the table
func (c *Context) NameSpace() string {
	return c.t.GetNameSpace()
}

// ParentTableUUID returns the parent table uuid of the table
func (c *Context) ParentTableUUID() string {
	return c.t.GetParentTableUUID()
}

// PgSchemaName returns the pg schema name of the table
func (c *Context) PgSchemaName() string {
	return c.t.GetPgSchemaName()
}

// RelationType returns the relation type of the table
func (c *Context) RelationType() string {
	return c.t.GetRelationType()
}

// SizeBytes returns the size in bytes of the table
func (c *Context) SizeBytes() string {
	size, unit := util.HumanReadableSize(c.t.GetSizeBytes())
	return fmt.Sprintf("%0.2f %s", size, unit)
}

// TableID returns the table id of the table
func (c *Context) TableID() string {
	return c.t.GetTableID()
}

// TableSpace returns the table space of the table
func (c *Context) TableSpace() string {
	return c.t.GetTableSpace()
}

// TableType returns the table type of the table
func (c *Context) TableType() string {
	return c.t.GetTableType()
}

// WalSizeBytes returns the wal size in bytes of the table
func (c *Context) WalSizeBytes() string {
	size, unit := util.HumanReadableSize(c.t.GetWalSizeBytes())
	return fmt.Sprintf("%0.2f, %s", size, unit)
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.t)
}
