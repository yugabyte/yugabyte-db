/*
 * Copyright (c) YugaByte, Inc.
 */

package ear

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultEARListing = "table {{.Name}}\t{{.UUID}}\t{{.KeyProvider}}\t{{.InUse}}"

	keyProviderHeader = "Key Provider"
)

// Context for ear config outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	ear util.KMSConfig
}

// NewEARFormat for formatting output
func NewEARFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultEARListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of EARs
func Write(ctx formatter.Context, ears []util.KMSConfig) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of ears into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(ears, "", "  ")
		} else {
			output, err = json.Marshal(ears)
		}

		if err != nil {
			logrus.Errorf("Error marshaling EAR configs to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, ear := range ears {
			err := format(&Context{ear: ear})
			if err != nil {
				logrus.Debugf("Error rendering ear config: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewEARContext(), render)
}

// NewEARContext creates a new context for rendering ear
func NewEARContext() *Context {
	earCtx := Context{}
	earCtx.Header = formatter.SubHeaderContext{
		"Name":        formatter.NameHeader,
		"UUID":        formatter.UUIDHeader,
		"KeyProvider": keyProviderHeader,
		"InUse":       formatter.InUseHeader,
	}
	return &earCtx
}

// UUID fetches EAR UUID
func (c *Context) UUID() string {
	return c.ear.ConfigUUID
}

// Name fetches EAR Name
func (c *Context) Name() string {
	return c.ear.Name
}

// InUse fetches EAR In Use
func (c *Context) InUse() string {
	return fmt.Sprintf("%t", c.ear.InUse)
}

// KeyProvider fetches EAR Key Provider
func (c *Context) KeyProvider() string {
	return c.ear.KeyProvider
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.ear)
}
