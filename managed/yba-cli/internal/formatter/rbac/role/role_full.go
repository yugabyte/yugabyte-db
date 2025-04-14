/*
 * Copyright (c) YugaByte, Inc.
 */

package role

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/permissioninfo"
)

const (
	// Role details
	roleDetails1 = "table {{.Description}}"
	roleDetails2 = "table {{.CreatedOn}}\t{{.UpdatedOn}}"
)

// FullRoleContext to render Role Details output
type FullRoleContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.Role
}

// SetFullRole initializes the context with the role data
func (fr *FullRoleContext) SetFullRole(role ybaclient.Role) {
	fr.r = role
}

// NewFullRoleFormat for formatting output
func NewFullRoleFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultRoleListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullRoleContext struct {
	Role *Context
}

// Write populates the output table to be displayed in the command line
func (fr *FullRoleContext) Write() error {
	var err error
	frc := &fullRoleContext{
		Role: &Context{},
	}
	frc.Role.r = fr.r

	// Section 1
	tmpl, err := fr.startSubsection(defaultRoleListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
	if err := fr.ContextFormat(tmpl, frc.Role); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewRoleContext())
	fr.Output.Write([]byte("\n"))

	// Section 2: Role Details subSection 1
	tmpl, err = fr.startSubsection(roleDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.subSection("Role Details")
	if err := fr.ContextFormat(tmpl, frc.Role); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewRoleContext())
	fr.Output.Write([]byte("\n"))

	tmpl, err = fr.startSubsection(roleDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fr.ContextFormat(tmpl, frc.Role); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fr.PostFormat(tmpl, NewRoleContext())
	fr.Output.Write([]byte("\n"))

	// Permission subSection
	permissionDetails := fr.r.GetPermissionDetails()
	permissionsPrereqLen := len(permissionDetails.GetPermissionList())
	if permissionsPrereqLen > 0 {
		logrus.Debugf("Number of permissions: %d", permissionsPrereqLen)
		fr.subSection("Permissions")
		for i, v := range permissionDetails.GetPermissionList() {
			permissionContext := *permissioninfo.NewPermissionContext()
			permissionContext.Output = os.Stdout
			permissionContext.Format = NewFullRoleFormat(viper.GetString("output"))
			permissionContext.SetPermission(v)
			permissionContext.Write(i)
		}
	}
	return nil
}

func (fr *FullRoleContext) startSubsection(format string) (*template.Template, error) {
	fr.Buffer = bytes.NewBufferString("")
	fr.ContextHeader = ""
	fr.Format = formatter.Format(format)
	fr.PreFormat()

	return fr.ParseFormat()
}

func (fr *FullRoleContext) subSection(name string) {
	fr.Output.Write([]byte("\n"))
	fr.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fr.Output.Write([]byte("\n"))
}

// NewFullRoleContext creates a new context for rendering role
func NewFullRoleContext() *FullRoleContext {
	roleCtx := FullRoleContext{}
	roleCtx.Header = formatter.SubHeaderContext{}
	return &roleCtx
}

// MarshalJSON function
func (fr *FullRoleContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fr.r)
}
