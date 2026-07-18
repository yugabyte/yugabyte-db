/*
 * Copyright (c) YugabyteDB, Inc.
 */

package permissioninfo

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultPermissionInfoListing = "table {{.Name}}\t{{.ResourceType}}\t" +
		"{{.Action}}\t{{.PermissionValidOnResource}}"

	resourceTypeHeader = "Resource Type"
	actionHeader       = "Action"
	permissionHeader   = "Permission Valid On Resource"

	prerequisitePermissionsHeader = "Prerequisite Permissions"
)

// Context for permission outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	p ybaclient.PermissionInfo
}

// NewPermissionInfoFormat for formatting output
func NewPermissionInfoFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultPermissionInfoListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of PermissionInfos
func Write(ctx formatter.Context, permissions []ybaclient.PermissionInfo) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of permissions into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(permissions, "", "  ")
		} else {
			output, err = json.Marshal(permissions)
		}

		if err != nil {
			logrus.Errorf("Error marshaling permissions to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, permission := range permissions {
			err := format(&Context{p: permission})
			if err != nil {
				logrus.Debugf("Error rendering permission: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewPermissionInfoContext(), render)
}

// NewPermissionInfoContext creates a new context for rendering permission
func NewPermissionInfoContext() *Context {
	permissionCtx := Context{}
	permissionCtx.Header = formatter.SubHeaderContext{
		"Name":                      formatter.NameHeader,
		"ResourceType":              resourceTypeHeader,
		"Action":                    actionHeader,
		"PermissionValidOnResource": permissionHeader,
		"Description":               formatter.DescriptionHeader,
		"PrerequisitePermissions":   prerequisitePermissionsHeader,
	}
	return &permissionCtx
}

// Name fetches PermissionInfo Name
func (c *Context) Name() string {
	return c.p.GetName()
}

// ResourceType fetches PermissionInfo ResourceType
func (c *Context) ResourceType() string {
	return c.p.GetResourceType()
}

// Action fetches PermissionInfo Action
func (c *Context) Action() string {
	return c.p.GetAction()
}

// PermissionValidOnResource fetches PermissionInfo PermissionValidOnResource
func (c *Context) PermissionValidOnResource() string {
	return fmt.Sprintf("%t", c.p.GetPermissionValidOnResource())
}

// Description fetches PermissionInfo Description
func (c *Context) Description() string {
	return c.p.GetDescription()
}

// PrerequisitePermissions fetches Permission PrerequisitePermissions
func (c *Context) PrerequisitePermissions() string {
	permissionPrereq := ""

	for _, p := range c.p.GetPrerequisitePermissions() {
		permissionPrereq = fmt.Sprintf(
			"%sAction: %s, Resource Type: %s\n",
			permissionPrereq,
			p.GetAction(),
			p.GetResourceType(),
		)
	}
	return permissionPrereq
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.p)
}
