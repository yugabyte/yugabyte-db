/*
 * Copyright (c) YugabyteDB, Inc.
 */

package keyinfo

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultKeyInfoListing = "table {{.Key}}\t{{.Name}}\t{{.Scope}}\t{{.ConfDataType}}"

	keyInfo1 = "table {{.Tags}}\t{{.HelpText}}"

	keyHeader          = "Key"
	scopeHeader        = "Scope"
	helpTextHeader     = "Help Text"
	confDataTypeHeader = "Data Type"
	tagsHeader         = "Tags"
)

// Context for keyInfo outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	ki ybaclient.ConfKeyInfo
}

// NewKeyInfoFormat for formatting output
func NewKeyInfoFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultKeyInfoListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of KeyInfos
func Write(ctx formatter.Context, keyInfos []ybaclient.ConfKeyInfo) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of keyInfos into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(keyInfos, "", "  ")
		} else {
			output, err = json.Marshal(keyInfos)
		}

		if err != nil {
			logrus.Errorf("Error marshaling key infos to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, keyInfo := range keyInfos {
			err := format(&Context{ki: keyInfo})
			if err != nil {
				logrus.Debugf("Error rendering key info: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewKeyInfoContext(), render)
}

// NewKeyInfoContext creates a new context for rendering keyInfo
func NewKeyInfoContext() *Context {
	keyInfoCtx := Context{}
	keyInfoCtx.Header = formatter.SubHeaderContext{
		"Name":         formatter.NameHeader,
		"Key":          keyHeader,
		"Scope":        scopeHeader,
		"HelpText":     helpTextHeader,
		"ConfDataType": confDataTypeHeader,
		"Tags":         tagsHeader,
	}
	return &keyInfoCtx
}

// Name fetches KeyInfo Name
func (c *Context) Name() string {
	return c.ki.GetDisplayName()
}

// Key fetches KeyInfo Key
func (c *Context) Key() string {
	return c.ki.GetKey()
}

// Scope fetches KeyInfo Scope
func (c *Context) Scope() string {
	return c.ki.GetScope()
}

// HelpText fetches KeyInfo HelpText
func (c *Context) HelpText() string {
	return c.ki.GetHelpTxt()
}

// ConfDataType fetches KeyInfo ConfDataType
func (c *Context) ConfDataType() string {
	dataType := c.ki.GetDataType()
	return dataType.GetName()
}

// Tags fetches KeyInfo Tags
func (c *Context) Tags() string {
	tags := "-"
	for i, v := range c.ki.GetTags() {
		if i == 0 {
			tags = v
		} else {
			tags = fmt.Sprintf("%s, %s", tags, v)
		}
	}
	return tags
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.ki)
}
