/*
 * Copyright (c) YugabyteDB, Inc.
 */

package key

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/scope"
)

const (
	defaultKeyListing = "table {{.Key}}\t{{.Value}}"
)

// Key for key outputs
type Key struct {
	Key   string `json:"key"`
	Value string `json:"value"`
}

// Context for key outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	k Key
}

// NewKeyFormat for formatting output
func NewKeyFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultKeyListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Keys
func Write(ctx formatter.Context, keys []Key) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of keys into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(keys, "", "  ")
		} else {
			output, err = json.Marshal(keys)
		}

		if err != nil {
			logrus.Errorf("Error marshaling key to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, key := range keys {
			err := format(&Context{k: key})
			if err != nil {
				logrus.Debugf("Error rendering key: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewKeyContext(), render)
}

// NewKeyContext creates a new context for rendering key
func NewKeyContext() *Context {
	keyCtx := Context{}
	keyCtx.Header = formatter.SubHeaderContext{
		"Key":   scope.KeyHeader,
		"Value": scope.ValueHeader,
	}
	return &keyCtx
}

// Key function
func (c *Context) Key() string {
	return c.k.Key
}

// Value function
func (c *Context) Value() string {
	return c.k.Value
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.k)
}
