/*
 * Copyright (c) YugabyteDB, Inc.
 */

package permissioninfo

import (
	"bytes"
	"encoding/json"
	"fmt"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (

	// Permission Details
	defaultPermission = "table {{.ResourceType}}\t{{.Action}}"
)

// PermissionContext for permission outputs
type PermissionContext struct {
	formatter.HeaderContext
	formatter.Context
	p ybaclient.Permission
}

// NewPermissionFormat for formatting output
func NewPermissionFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultPermission
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetPermission initializes the context with the permission data
func (p *PermissionContext) SetPermission(permission ybaclient.Permission) {
	p.p = permission
}

type permissionContext struct {
	Permission *PermissionContext
}

// Write populates the output table to be displayed in the command line
func (p *PermissionContext) Write(index int) error {
	var err error
	rc := &permissionContext{
		Permission: &PermissionContext{},
	}
	rc.Permission.p = p.p

	// Section 1
	tmpl, err := p.startSubsection(defaultPermission)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	p.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Permission %d: Details", index+1), formatter.BlueColor)))
	p.Output.Write([]byte("\n"))
	if err := p.ContextFormat(tmpl, rc.Permission); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	p.PostFormat(tmpl, NewPermissionContext())

	p.Output.Write([]byte("\n"))

	return nil
}

func (p *PermissionContext) startSubsection(format string) (*template.Template, error) {
	p.Buffer = bytes.NewBufferString("")
	p.ContextHeader = ""
	p.Format = formatter.Format(format)
	p.PreFormat()

	return p.ParseFormat()
}

func (p *PermissionContext) subSection(name string) {
	p.Output.Write([]byte("\n"))
	p.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	p.Output.Write([]byte("\n"))
}

// NewPermissionContext creates a new context for rendering permissions
func NewPermissionContext() *PermissionContext {
	permissionCtx := PermissionContext{}
	permissionCtx.Header = formatter.SubHeaderContext{
		"Action":       actionHeader,
		"ResourceType": resourceTypeHeader,
	}
	return &permissionCtx
}

// Action fetches PermissionInfo Action
func (p *PermissionContext) Action() string {
	return p.p.GetAction()
}

// ResourceType fetches PermissionInfo ResourceType
func (p *PermissionContext) ResourceType() string {
	return p.p.GetResourceType()
}

// MarshalJSON function
func (p *PermissionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(p.p)
}
