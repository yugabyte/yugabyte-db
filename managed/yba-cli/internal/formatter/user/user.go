/*
 * Copyright (c) YugabyteDB, Inc.
 */

package user

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultUserListing = "table {{.Email}}\t{{.UUID}}\t{{.Role}}\t" +
		"{{.UserType}}\t{{.Primary}}\t{{.CreationDate}}"

	emailHeader            = "Email"
	roleHeader             = "Role"
	userTypeHeader         = "User Type"
	primaryHeader          = "Primary"
	creationDateHeader     = "Creation Date"
	timezoneHeader         = "Time Zone"
	ldapRoleHeader         = "LDAP Role"
	oidcJwtAuthTokenHeader = "OIDC JWT Auth Token"
	groupMembershipsHeader = "Group Memberships"
)

// RoleBindings for the user
var RoleBindings map[string][]ybaclient.RoleBinding

// Context for user outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	u ybaclient.UserWithFeatures
}

// NewUserFormat for formatting output
func NewUserFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultUserListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Users
func Write(ctx formatter.Context, users []ybaclient.UserWithFeatures) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of users into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(users, "", "  ")
		} else {
			output, err = json.Marshal(users)
		}

		if err != nil {
			logrus.Errorf("Error marshaling users to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}

	// Existing logic for table and other formats
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, user := range users {
			err := format(&Context{u: user})
			if err != nil {
				logrus.Debugf("Error rendering user: %v\n", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewUserContext(), render)
}

// NewUserContext creates a new context for rendering user
func NewUserContext() *Context {
	userCtx := Context{}
	userCtx.Header = formatter.SubHeaderContext{
		"Email":            emailHeader,
		"UUID":             formatter.UUIDHeader,
		"Role":             roleHeader,
		"UserType":         userTypeHeader,
		"Primary":          primaryHeader,
		"CreationDate":     creationDateHeader,
		"TimeZone":         timezoneHeader,
		"LDAPRole":         ldapRoleHeader,
		"OidcJwtAuthToken": oidcJwtAuthTokenHeader,
		"GroupMemberships": groupMembershipsHeader,
	}
	return &userCtx
}

// UUID fetches User UUID
func (c *Context) UUID() string {
	return c.u.GetUuid()
}

// Email fetches User Email
func (c *Context) Email() string {
	return c.u.GetEmail()
}

// Role fetches User Role
func (c *Context) Role() string {
	roles := "-"
	roleBindings := RoleBindings[c.u.GetUuid()]
	if len(roleBindings) == 0 {
		return roles
	}
	for i, roleBinding := range roleBindings {
		roleInRB := roleBinding.GetRole()
		roleName := roleInRB.GetName()
		if i == 0 {
			roles = roleName
		} else {
			roles = fmt.Sprintf("%s, %s", roles, roleName)
		}
	}
	return roles
}

// UserType fetches User Type
func (c *Context) UserType() string {
	return c.u.GetUserType()
}

// Primary fetches User Primary
func (c *Context) Primary() string {
	return fmt.Sprintf("%t", c.u.GetPrimary())
}

// CreationDate fetches User Creation Date
func (c *Context) CreationDate() string {
	return util.PrintTime(c.u.GetCreationDate())
}

// TimeZone fetches User Timezone
func (c *Context) TimeZone() string {
	return c.u.GetTimezone()
}

// LDAPRole fetches User LDAP Role
func (c *Context) LDAPRole() string {
	return fmt.Sprintf("%t", c.u.GetLdapSpecifiedRole())
}

// OidcJwtAuthToken fetches User OIDC JWT Auth Token
func (c *Context) OidcJwtAuthToken() string {
	return c.u.GetOidcJwtAuthToken()
}

// GroupMemberships fetches User Group Memberships
func (c *Context) GroupMemberships() string {
	group := ""
	for i, membership := range c.u.GetGroupMemberships() {
		if i == 0 {
			group = membership
		} else {
			group = fmt.Sprintf("%s, %s", group, membership)
		}
	}
	return group
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	jsonBytes, err := json.Marshal(c.u)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal struct: %w", err)
	}

	var result map[string]interface{}
	if err := json.Unmarshal(jsonBytes, &result); err != nil {
		return nil, fmt.Errorf("failed to unmarshal JSON to map: %w", err)
	}
	result["role"] = c.Role()
	return json.Marshal(result)
}
