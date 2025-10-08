/*
 * Copyright (c) YugabyteDB, Inc.
 */

package permissioninfo

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// PermissionInfo details
	descriptionTable = "table {{.Description}}"
)

// FullPermissionInfoContext to render PermissionInfo Details output
type FullPermissionInfoContext struct {
	formatter.HeaderContext
	formatter.Context
	p ybaclient.PermissionInfo
}

// SetFullPermissionInfo initializes the context with the permission data
func (fp *FullPermissionInfoContext) SetFullPermissionInfo(permission ybaclient.PermissionInfo) {
	fp.p = permission
}

// NewFullPermissionInfoFormat for formatting output
func NewFullPermissionInfoFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultPermissionInfoListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullPermissionInfoContext struct {
	PermissionInfo *Context
}

// Write populates the output table to be displayed in the command line
func (fp *FullPermissionInfoContext) Write() error {
	var err error
	fpc := &fullPermissionInfoContext{
		PermissionInfo: &Context{},
	}
	fpc.PermissionInfo.p = fp.p

	// Section 1
	tmpl, err := fp.startSubsection(defaultPermissionInfoListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fp.Output.Write([]byte("\n"))
	if err := fp.ContextFormat(tmpl, fpc.PermissionInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewPermissionInfoContext())
	fp.Output.Write([]byte("\n"))

	// Section 2: PermissionInfo Details subSection 1
	tmpl, err = fp.startSubsection(descriptionTable)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.subSection("Permission Details")
	if err := fp.ContextFormat(tmpl, fpc.PermissionInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewPermissionInfoContext())
	fp.Output.Write([]byte("\n"))

	// Permission subSection
	permissionsPrereqLen := len(fp.p.GetPrerequisitePermissions())
	if permissionsPrereqLen > 0 {
		logrus.Debugf("Number of prerequisite permissions: %d", permissionsPrereqLen)
		fp.subSection("Prerequisite Permissions")
		for i, v := range fp.p.GetPrerequisitePermissions() {
			permissionContext := *NewPermissionContext()
			permissionContext.Output = os.Stdout
			permissionContext.Format = NewFullPermissionInfoFormat(viper.GetString("output"))
			permissionContext.SetPermission(v)
			permissionContext.Write(i)
		}
	}
	return nil
}

func (fp *FullPermissionInfoContext) startSubsection(format string) (*template.Template, error) {
	fp.Buffer = bytes.NewBufferString("")
	fp.ContextHeader = ""
	fp.Format = formatter.Format(format)
	fp.PreFormat()

	return fp.ParseFormat()
}

func (fp *FullPermissionInfoContext) subSection(name string) {
	fp.Output.Write([]byte("\n"))
	fp.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fp.Output.Write([]byte("\n"))
}

// NewFullPermissionInfoContext creates a new context for rendering permission
func NewFullPermissionInfoContext() *FullPermissionInfoContext {
	permissionCtx := FullPermissionInfoContext{}
	permissionCtx.Header = formatter.SubHeaderContext{}
	return &permissionCtx
}

// MarshalJSON function
func (fp *FullPermissionInfoContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fp.p)
}
