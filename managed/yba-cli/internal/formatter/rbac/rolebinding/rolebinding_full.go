/*
 * Copyright (c) YugaByte, Inc.
 */

package rolebinding

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/role"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/user"
)

const (
	// RoleBinding details
	defaultRoleBindingFull = "table {{.UUID}}\t{{.Type}}\t{{.CreateTime}}\t{{.UpdateTime}}"
)

// FullRoleBindingContext to render RoleBinding Details output
type FullRoleBindingContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.RoleBinding
}

// SetFullRoleBinding initializes the context with the roleBinding data
func (fr *FullRoleBindingContext) SetFullRoleBinding(roleBinding ybaclient.RoleBinding) {
	fr.r = roleBinding
}

// NewFullRoleBindingFormat for formatting output
func NewFullRoleBindingFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultRoleBindingListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullRoleBindingContext struct {
	RoleBinding      *Context
	Principal        *PrincipalContext
	GroupMappingInfo *GroupMappingInfoContext
}

// Write populates the output table to be displayed in the command line
func (fr *FullRoleBindingContext) Write() error {
	var err error
	frc := &fullRoleBindingContext{
		RoleBinding: &Context{},
	}
	frc.RoleBinding.r = fr.r
	frc.Principal = &PrincipalContext{
		P: fr.r.GetPrincipal(),
	}
	frc.GroupMappingInfo = &GroupMappingInfoContext{
		Gmi: fr.r.GetGroupInfo(),
	}

	// Section 1
	tmpl, err := fr.startSubsection(defaultRoleBindingFull)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
	if err := fr.ContextFormat(tmpl, frc.RoleBinding); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewRoleBindingContext())
	fr.Output.Write([]byte("\n"))

	tmpl, err = fr.startSubsection(principalDetails)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.subSection("Principal")
	if err := fr.ContextFormat(tmpl, frc.Principal); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewPrincipalContext())

	fr.subSection("Role")
	roleCtx := formatter.Context{
		Command: "describe",
		Output:  os.Stdout,
		Format:  role.NewRoleFormat(viper.GetString("output")),
	}
	role.Write(roleCtx, []ybaclient.Role{fr.r.GetRole()})

	userInfo := fr.r.GetUser()
	if len(userInfo.GetUuid()) != 0 {
		userDetails := ybaclient.UserWithFeatures{
			Uuid:               userInfo.Uuid,
			Email:              userInfo.GetEmail(),
			AuthTokenIssueDate: userInfo.AuthTokenIssueDate,
			CreationDate:       userInfo.CreationDate,
			CustomerUUID:       userInfo.CustomerUUID,
			GroupMemberships:   userInfo.GroupMemberships,
			LdapSpecifiedRole:  userInfo.LdapSpecifiedRole,
			OidcJwtAuthToken:   userInfo.OidcJwtAuthToken,
			Primary:            userInfo.GetPrimary(),
			Role:               userInfo.Role,
			Timezone:           userInfo.Timezone,
			UserType:           userInfo.UserType,
		}
		fr.subSection("User")
		userCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  user.NewUserFormat(viper.GetString("output")),
		}
		user.Write(userCtx, []ybaclient.UserWithFeatures{userDetails})
	}

	groupInfo := fr.r.GetGroupInfo()
	if len(groupInfo.GetGroupUUID()) != 0 {
		tmpl, err = fr.startSubsection(groupMappingInfoDetails)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fr.subSection("Group Mapping Info")
		if err := fr.ContextFormat(tmpl, frc.GroupMappingInfo); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fr.PostFormat(tmpl, NewGroupMappingInfoContext())
	}

	// Resource Group section
	resourceGroups := fr.r.GetResourceGroup()
	logrus.Debugf("Number of Resource Groups: %d", len(resourceGroups.GetResourceDefinitionSet()))
	fr.subSection("Resource Groups")
	for i, v := range resourceGroups.GetResourceDefinitionSet() {
		resourceGroupContext := *NewResourceGroupContext()
		resourceGroupContext.Output = os.Stdout
		resourceGroupContext.Format = NewFullRoleBindingFormat(viper.GetString("output"))
		resourceGroupContext.SetResourceGroup(v)
		resourceGroupContext.Write(i)
	}

	return nil
}

func (fr *FullRoleBindingContext) startSubsection(format string) (*template.Template, error) {
	fr.Buffer = bytes.NewBufferString("")
	fr.ContextHeader = ""
	fr.Format = formatter.Format(format)
	fr.PreFormat()

	return fr.ParseFormat()
}

func (fr *FullRoleBindingContext) subSection(name string) {
	fr.Output.Write([]byte("\n"))
	fr.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
}

// NewFullRoleBindingContext creates a new context for rendering roleBinding
func NewFullRoleBindingContext() *FullRoleBindingContext {
	roleBindingCtx := FullRoleBindingContext{}
	roleBindingCtx.Header = formatter.SubHeaderContext{}
	return &roleBindingCtx
}

// MarshalJSON function
func (fr *FullRoleBindingContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fr.r)
}
