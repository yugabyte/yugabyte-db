/*
 * Copyright (c) YugaByte, Inc.
 */

package scope

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultScopeListing = "table {{.UUID}}\t{{.Type}}\t{{.MutableScope}}"

	mutableScopeHeader    = "Mutable Scope"
	numberOfConfigsHeader = "Number of Configurations"
)

// Context for scope outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	s ybaclient.ScopedConfig
}

// NewScopeFormat for formatting output
func NewScopeFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultScopeListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Scopes
func Write(ctx formatter.Context, scopes []ybaclient.ScopedConfig) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of scopes into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(scopes, "", "  ")
		} else {
			output, err = json.Marshal(scopes)
		}

		if err != nil {
			logrus.Errorf("Error marshaling scope to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, scope := range scopes {
			err := format(&Context{s: scope})
			if err != nil {
				logrus.Debugf("Error rendering scope: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewScopeContext(), render)
}

// NewScopeContext creates a new context for rendering scope
func NewScopeContext() *Context {
	scopeCtx := Context{}
	scopeCtx.Header = formatter.SubHeaderContext{
		"UUID":            formatter.UUIDHeader,
		"Type":            formatter.TypeHeader,
		"MutableScope":    mutableScopeHeader,
		"NumberOfConfigs": numberOfConfigsHeader,
	}
	return &scopeCtx
}

// UUID fetches scope UUID
func (c *Context) UUID() string {
	return c.s.GetUuid()
}

// Type fetches scope type
func (c *Context) Type() string {
	return c.s.GetType()
}

// MutableScope fetches scope mutable scope
func (c *Context) MutableScope() string {
	return fmt.Sprintf("%t", c.s.GetMutableScope())
}

// NumberOfConfigs fetches number of configs
func (c *Context) NumberOfConfigs() string {
	return fmt.Sprintf("%d", len(c.s.GetConfigEntries()))
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.s)
}
