/*
 * Copyright (c) YugabyteDB, Inc.
 */

package rolebinding

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// PrincipalDetails for principal details
	principalDetails = "table {{.UUID}}\t{{.Type}}\t{{.UserUUID}}\t{{.GroupUUID}}"

	userUUIDHeader  = "User UUID"
	groupUUIDHeader = "Group UUID"
)

// PrincipalContext for principal outputs
type PrincipalContext struct {
	formatter.HeaderContext
	formatter.Context
	P ybaclient.Principal
}

// NewPrincipalFormat for formatting output
func NewPrincipalFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := principalDetails
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewPrincipalContext creates a new context for rendering principal
func NewPrincipalContext() *PrincipalContext {
	principalCtx := PrincipalContext{}
	principalCtx.Header = formatter.SubHeaderContext{
		"UUID":      formatter.UUIDHeader,
		"Type":      formatter.TypeHeader,
		"GroupUUID": groupUUIDHeader,
		"UserUUID":  userUUIDHeader,
	}
	return &principalCtx
}

// UUID function
func (c *PrincipalContext) UUID() string {
	return c.P.GetUuid()
}

// Type function
func (c *PrincipalContext) Type() string {
	return c.P.GetType()
}

// UserUUID function
func (c *PrincipalContext) UserUUID() string {
	return c.P.GetUserUUID()
}

// GroupUUID function
func (c *PrincipalContext) GroupUUID() string {
	return c.P.GetGroupUUID()
}

// MarshalJSON function
func (c *PrincipalContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.P)
}
