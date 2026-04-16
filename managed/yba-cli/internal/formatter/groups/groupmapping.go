/*
 * Copyright (c) YugabyteDB, Inc.
 */

package groupmapping

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultGroupMappingListing = "table {{.Name}}\t{{.UUID}}\t{{.Type}}\t{{.CreationDate}}\t{{.Roles}}"
)

// Context for groupMapping outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	gm          ybav2client.AuthGroupToRolesMapping
	roleNameMap *map[string]string
}

// NewGroupMappingFormat for formatting output
func NewGroupMappingFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultGroupMappingListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of GroupMappings
func Write(
	ctx formatter.Context,
	providers []ybav2client.AuthGroupToRolesMapping,
	roleNamesMap *map[string]string,
) error {
	// Handle JSON or Pretty JSON formats
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		output, err := func() ([]byte, error) {
			if ctx.Format.IsPrettyJSON() {
				return json.MarshalIndent(providers, "", "  ")
			}
			return json.Marshal(providers)
		}()
		if err != nil {
			logrus.Errorf("Error marshaling providers to JSON: %v", err)
			return err
		}
		_, err = ctx.Output.Write(output)
		return err
	}
	// Handle other formats
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, groupMapping := range providers {
			if err := format(&Context{gm: groupMapping, roleNameMap: roleNamesMap}); err != nil {
				logrus.Debugf("Error rendering group mapping: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewGroupMappingContext(), render)
}

// NewGroupMappingContext creates a new context for rendering groupMapping
func NewGroupMappingContext() *Context {
	providerCtx := Context{}
	providerCtx.Header = formatter.SubHeaderContext{
		"Name":         formatter.NameHeader,
		"UUID":         formatter.UUIDHeader,
		"Type":         formatter.TypeHeader,
		"CreationDate": formatter.CreateTimeHeader,
		"Roles":        "Roles",
	}
	return &providerCtx
}

// UUID fetches GroupMapping UUID
func (c *Context) UUID() string {
	return c.gm.GetUuid()
}

// Name fetches GroupMapping Name
func (c *Context) Name() string {
	// Group name incase of OIDC. Group DN incase of LDAP
	return c.gm.GetGroupIdentifier()
}

// Type fetches GroupMapping type
func (c *Context) Type() string {
	// OIDC or LDAP
	return c.gm.GetType()
}

// CreationDate fetches GroupMapping Creation Date
func (c *Context) CreationDate() string {
	return util.PrintTime(c.gm.GetCreationDate())
}

// Roles fetches GroupMapping Roles
func (c *Context) Roles() []string {
	roles := c.gm.GetRoleResourceDefinitions()
	if len(roles) == 0 {
		return nil
	}
	roleNames := make([]string, len(roles))
	for i, role := range roles {
		roleNames[i] = c.GetRoleName(role.GetRoleUuid()) + " (" + role.GetRoleUuid() + ")"
	}
	return roleNames
}

// GetRoleName fetches GroupMapping Role Name
func (c *Context) GetRoleName(roleUUID string) string {
	if c.roleNameMap == nil {
		return roleUUID
	}
	if roleName, exists := (*c.roleNameMap)[roleUUID]; exists {
		return roleName
	}
	return roleUUID
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.gm)
}
