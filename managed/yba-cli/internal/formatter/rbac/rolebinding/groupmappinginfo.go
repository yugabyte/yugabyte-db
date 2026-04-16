/*
 * Copyright (c) YugabyteDB, Inc.
 */

package rolebinding

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// GroupMappingInfoDetails for groupMappingInfo details
	groupMappingInfoDetails = "table {{.UUID}}\t{{.Type}}\t{{.Identifier}}\t{{.CreationDate}}"

	creationDateHeader = "Creation Date"
	identifierHeader   = "Identifier"
)

// GroupMappingInfoContext for group mapping info outputs
type GroupMappingInfoContext struct {
	formatter.HeaderContext
	formatter.Context
	Gmi ybaclient.GroupMappingInfo
}

// NewGroupMappingInfoFormat for formatting output
func NewGroupMappingInfoFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := groupMappingInfoDetails
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewGroupMappingInfoContext creates a new context for rendering group mapping info
func NewGroupMappingInfoContext() *GroupMappingInfoContext {
	groupMappingInfoCtx := GroupMappingInfoContext{}
	groupMappingInfoCtx.Header = formatter.SubHeaderContext{
		"UUID":         formatter.UUIDHeader,
		"Type":         formatter.TypeHeader,
		"Identifier":   identifierHeader,
		"CreationDate": creationDateHeader,
	}
	return &groupMappingInfoCtx
}

// UUID function
func (c *GroupMappingInfoContext) UUID() string {
	return c.Gmi.GetGroupUUID()
}

// Type function
func (c *GroupMappingInfoContext) Type() string {
	return c.Gmi.GetType()
}

// Identifier function
func (c *GroupMappingInfoContext) Identifier() string {
	return c.Gmi.GetIdentifier()
}

// CreationDate function
func (c *GroupMappingInfoContext) CreationDate() string {
	return util.PrintTime(c.Gmi.GetCreationDate())
}

// MarshalJSON function
func (c *GroupMappingInfoContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Gmi)
}
