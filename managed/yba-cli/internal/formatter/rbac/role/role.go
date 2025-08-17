/*
 * Copyright (c) YugaByte, Inc.
 */

package role

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultRoleListing      = "table {{.Name}}\t{{.UUID}}\t{{.RoleType}}"
	roleTypeHeader          = "Role Type"
	permissionDetailsHeader = "Permissions"

	createdOnHeader = "Created On"
	updatedOnHeader = "Updated On"
)

// Context for role outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.Role
}

// NewRoleFormat for formatting output
func NewRoleFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultRoleListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Roles
func Write(ctx formatter.Context, roles []ybaclient.Role) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of roles into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(roles, "", "  ")
		} else {
			output, err = json.Marshal(roles)
		}

		if err != nil {
			logrus.Errorf("Error marshaling roles to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, role := range roles {
			err := format(&Context{r: role})
			if err != nil {
				logrus.Debugf("Error rendering role: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewRoleContext(), render)
}

// NewRoleContext creates a new context for rendering role
func NewRoleContext() *Context {
	roleCtx := Context{}
	roleCtx.Header = formatter.SubHeaderContext{
		"Name":              formatter.NameHeader,
		"UUID":              formatter.UUIDHeader,
		"RoleType":          roleTypeHeader,
		"Description":       formatter.DescriptionHeader,
		"PermissionDetails": permissionDetailsHeader,
		"CreatedOn":         createdOnHeader,
		"UpdatedOn":         updatedOnHeader,
	}
	return &roleCtx
}

// UUID fetches Role UUID
func (c *Context) UUID() string {
	return c.r.GetRoleUUID()
}

// Name fetches Role Name
func (c *Context) Name() string {
	return c.r.GetName()
}

// RoleType fetches Role Type
func (c *Context) RoleType() string {
	return c.r.GetRoleType()
}

// Description fetches Role Description
func (c *Context) Description() string {
	return c.r.GetDescription()
}

// PermissionDetails fetches Role PermissionDetails
func (c *Context) PermissionDetails() string {
	permission := ""
	permissionDetails := c.r.GetPermissionDetails()
	for _, p := range permissionDetails.GetPermissionList() {
		permission = fmt.Sprintf(
			"%sAction: %s, Resource Type: %s\n",
			permission, p.GetAction(), p.GetResourceType())
	}
	return permission
}

// CreatedOn fetches Role CreatedOn
func (c *Context) CreatedOn() string {
	return util.PrintTime(c.r.GetCreatedOn())
}

// UpdatedOn fetches Role UpdatedOn
func (c *Context) UpdatedOn() string {
	return util.PrintTime(c.r.GetUpdatedOn())
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.r)
}
