/*
 * Copyright (c) YugaByte, Inc.
 */

package ldap

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultLDAPConfigListing = "table {{.Key}}\t{{.Value}}"
	configHeader             = "Key"
	valueHeader              = "Value"
)

// Context for user outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	configEntry ybaclient.ConfigEntry
}

// NewLDAPFormat for formatting output
func NewLDAPFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultLDAPConfigListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of ldap config entries
func Write(ctx formatter.Context, ldapConfig []ybaclient.ConfigEntry) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of ldapConfig into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(ldapConfig, "", "  ")
		} else {
			output, err = json.Marshal(ldapConfig)
		}

		if err != nil {
			logrus.Errorf("Error marshaling ldap config entries to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}

	// Existing logic for table and other formats
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, configEntry := range ldapConfig {
			err := format(&Context{configEntry: configEntry})
			if err != nil {
				logrus.Debugf("Error rendering user: %v\n", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewLDAPContext(), render)
}

// NewLDAPContext creates a new context for rendering user
func NewLDAPContext() *Context {
	ldapConfigEntryCtx := Context{}
	ldapConfigEntryCtx.Header = formatter.SubHeaderContext{
		"Key":   configHeader,
		"Value": valueHeader,
	}
	return &ldapConfigEntryCtx
}

// Key fetches Key Name
func (c *Context) Key() string {
	if key, exists := util.LDAPKeyToFlagMap[c.configEntry.GetKey()]; exists {
		return key
	}
	return c.configEntry.GetKey()
}

// Value fetches Key Value
func (c *Context) Value() string {
	return c.configEntry.GetValue()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.configEntry)
}
