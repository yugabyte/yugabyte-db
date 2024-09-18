/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetypes

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultInstanceTypesListing = "table {{.Name}}\t{{.ProviderUUID}}\t{{.Cores}}\t{{.Memory}}"

	providerUUIDHeader = "Provider UUID"
	coresHeader        = "Cores"
	memoryHeader       = "Memory Size in GB"
)

// Context for instanceType outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	iT ybaclient.InstanceTypeResp
}

// NewInstanceTypesFormat for formatting output
func NewInstanceTypesFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultInstanceTypesListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of InstanceTypess
func Write(ctx formatter.Context, instanceTypes []ybaclient.InstanceTypeResp) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of instance types into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(instanceTypes, "", "  ")
		} else {
			output, err = json.Marshal(instanceTypes)
		}

		if err != nil {
			logrus.Errorf("Error marshaling instance types to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, instanceType := range instanceTypes {
			err := format(&Context{iT: instanceType})
			if err != nil {
				logrus.Debugf("Error rendering instanceType: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewInstanceTypesContext(), render)
}

// NewInstanceTypesContext creates a new context for rendering instanceType
func NewInstanceTypesContext() *Context {
	instanceTypeCtx := Context{}
	instanceTypeCtx.Header = formatter.SubHeaderContext{
		"Name":         formatter.NameHeader,
		"ProviderUUID": providerUUIDHeader,
		"Cores":        coresHeader,
		"Memory":       memoryHeader,
	}
	return &instanceTypeCtx
}

// Name fetches InstanceTypes Name
func (c *Context) Name() string {
	return c.iT.GetInstanceTypeCode()
}

// ProviderUUID fetched provider UUID
func (c *Context) ProviderUUID() string {
	return c.iT.GetProviderUuid()
}

// Cores fetches the cores associated with the instanceType
func (c *Context) Cores() string {
	return fmt.Sprintf("%0.00f", c.iT.GetNumCores())

}

// Memory fetches the memory associated with the instanceType
func (c *Context) Memory() string {
	return fmt.Sprintf("%0.00f", c.iT.GetMemSizeGB())

}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.iT)
}
