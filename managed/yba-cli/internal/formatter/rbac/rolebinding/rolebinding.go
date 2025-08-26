/*
 * Copyright (c) YugaByte, Inc.
 */

package rolebinding

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultRoleBindingListing = "table {{.UUID}}\t{{.PrincipalType}}\t{{.PrincipalUUID}}\t" +
		"{{.User}}\t{{.Role}}\t{{.GroupInfo}}"

	userHeader          = "User" // will have email (UUID)
	roleHeader          = "Role" // role name (UUID)
	groupInfoHeader     = "Group Info"
	principalUUIDHeader = "Principal UUID"
	principalTypeHeader = "Principal Type"
	resourceGroupHeader = "Resource Group"
)

// Context for roleBinding outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.RoleBinding
}

// NewRoleBindingFormat for formatting output
func NewRoleBindingFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultRoleBindingListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of RoleBindings
func Write(ctx formatter.Context, roleBindings []ybaclient.RoleBinding) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of roleBindings into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(roleBindings, "", "  ")
		} else {
			output, err = json.Marshal(roleBindings)
		}

		if err != nil {
			logrus.Errorf("Error marshaling role bindings to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, roleBinding := range roleBindings {
			err := format(&Context{r: roleBinding})
			if err != nil {
				logrus.Debugf("Error rendering roleBinding: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewRoleBindingContext(), render)
}

// NewRoleBindingContext creates a new context for rendering roleBinding
func NewRoleBindingContext() *Context {
	roleBindingCtx := Context{}
	roleBindingCtx.Header = formatter.SubHeaderContext{
		"CreateTime":    formatter.CreateTimeHeader,
		"UpdateTime":    formatter.UpdateTimeHeader,
		"GroupInfo":     groupInfoHeader,
		"User":          userHeader,
		"Role":          roleHeader,
		"PrincipalUUID": principalUUIDHeader,
		"PrincipalType": principalTypeHeader,
		"ResourceGroup": resourceGroupHeader,
		"Type":          formatter.TypeHeader,
		"UUID":          formatter.UUIDHeader,
	}
	return &roleBindingCtx
}

// CreateTime function to fetch CreateTime
func (c *Context) CreateTime() string {
	return util.PrintTime(c.r.GetCreateTime())
}

// UpdateTime function to fetch UpdateTime
func (c *Context) UpdateTime() string {
	return util.PrintTime(c.r.GetUpdateTime())
}

// GroupInfo function to fetch GroupInfo
func (c *Context) GroupInfo() string {
	groupInfo := c.r.GetGroupInfo()
	if len(groupInfo.GetIdentifier()) == 0 {
		return "-"
	}
	return groupInfo.GetIdentifier()
}

// User function to fetch User
func (c *Context) User() string {
	user := c.r.GetUser()
	if len(user.GetEmail()) == 0 {
		return "-"
	}
	return user.GetEmail()
}

// Role function to fetch Role
func (c *Context) Role() string {
	role := c.r.GetRole()
	return role.GetName()
}

// PrincipalType function to fetch Principal
func (c *Context) PrincipalType() string {
	principal := c.r.GetPrincipal()
	return principal.GetType()
}

// PrincipalUUID function to fetch Principal
func (c *Context) PrincipalUUID() string {
	principal := c.r.GetPrincipal()
	return principal.GetUuid()
}

// ResourceGroup function to fetch ResourceGroup
func (c *Context) ResourceGroup() string {
	resourceGroup := c.r.GetResourceGroup()
	resourceDefinitionSet := resourceGroup.GetResourceDefinitionSet()
	resourceDefinition := ""
	for i, r := range resourceDefinitionSet {
		if i == 0 {
			resourceDefinition = r.GetResourceType()
		} else {
			resourceDefinition = fmt.Sprintf("%s, %s", resourceDefinition, r.GetResourceType())
		}
	}
	return resourceDefinition
}

// Type function to fetch Type
func (c *Context) Type() string {
	return c.r.GetType()
}

// UUID function to fetch UUID
func (c *Context) UUID() string {
	return c.r.GetUuid()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.r)
}
