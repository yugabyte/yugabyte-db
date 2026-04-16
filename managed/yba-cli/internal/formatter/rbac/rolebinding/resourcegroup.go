/*
 * Copyright (c) YugabyteDB, Inc.
 */

package rolebinding

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

	// ResourceGroup Details
	defaultResourceGroup = "table {{.ResourceType}}\t{{.AllowAll}}"
	resourceGroupDetails = "table {{.ResourceUUIDs}}"

	resourceTypeHeader = "Resource Type"
	allowAllHeader     = "Allow All"

	resourceUUIDsHeader = "Resource UUIDs"
)

// ResourceGroupContext for resourceGroup outputs
type ResourceGroupContext struct {
	formatter.HeaderContext
	formatter.Context
	rg ybaclient.ResourceDefinition
}

// NewResourceGroupFormat for formatting output
func NewResourceGroupFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultResourceGroup
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetResourceGroup initializes the context with the resourceGroup data
func (rg *ResourceGroupContext) SetResourceGroup(resourceGroup ybaclient.ResourceDefinition) {
	rg.rg = resourceGroup
}

type resourceGroupContext struct {
	ResourceGroup *ResourceGroupContext
}

// Write populates the output table to be displayed in the command line
func (rg *ResourceGroupContext) Write(index int) error {
	var err error
	rc := &resourceGroupContext{
		ResourceGroup: &ResourceGroupContext{},
	}
	rc.ResourceGroup.rg = rg.rg

	// Section 1
	tmpl, err := rg.startSubsection(defaultResourceGroup)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	rg.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Resource Group %d: Details", index+1), formatter.BlueColor)))
	rg.Output.Write([]byte("\n"))
	if err := rg.ContextFormat(tmpl, rc.ResourceGroup); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	rg.PostFormat(tmpl, NewResourceGroupContext())

	rg.Output.Write([]byte("\n"))

	// Section 2: image bundle details
	tmpl, err = rg.startSubsection(resourceGroupDetails)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := rg.ContextFormat(tmpl, rc.ResourceGroup); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	rg.PostFormat(tmpl, NewResourceGroupContext())
	rg.Output.Write([]byte("\n"))

	return nil
}

func (rg *ResourceGroupContext) startSubsection(format string) (*template.Template, error) {
	rg.Buffer = bytes.NewBufferString("")
	rg.ContextHeader = ""
	rg.Format = formatter.Format(format)
	rg.PreFormat()

	return rg.ParseFormat()
}

func (rg *ResourceGroupContext) subSection(name string) {
	rg.Output.Write([]byte("\n"))
	rg.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	rg.Output.Write([]byte("\n"))
}

// NewResourceGroupContext creates a new context for rendering resourceGroups
func NewResourceGroupContext() *ResourceGroupContext {
	resourceGroupCtx := ResourceGroupContext{}
	resourceGroupCtx.Header = formatter.SubHeaderContext{
		"AllowAll":      allowAllHeader,
		"ResourceType":  resourceTypeHeader,
		"ResourceUUIDs": resourceUUIDsHeader,
	}
	return &resourceGroupCtx
}

// AllowAll function
func (rg *ResourceGroupContext) AllowAll() string {
	return fmt.Sprintf("%t", rg.rg.GetAllowAll())
}

// ResourceType function
func (rg *ResourceGroupContext) ResourceType() string {
	return rg.rg.GetResourceType()
}

// ResourceUUIDs function
func (rg *ResourceGroupContext) ResourceUUIDs() string {
	uuids := rg.rg.GetResourceUUIDSet()
	uuidString := ""
	for i, uuid := range uuids {
		if i == 0 {
			uuidString = uuid
		} else {
			uuidString = fmt.Sprintf("%s, %s", uuidString, uuid)
		}
	}
	if uuidString == "" {
		uuidString = "-"
	}
	return uuidString
}

// MarshalJSON function
func (rg *ResourceGroupContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(rg.rg)
}
