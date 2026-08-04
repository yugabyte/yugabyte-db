/*
 * Copyright (c) YugabyteDB, Inc.
 */

package scope

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

	// ConfigEntry Details
	defaultConfigEntry = "table {{.Inherited}}\t{{.Key}}\t{{.Value}}"

	inheritedHeader = "Inherited"

	// KeyHeader ConfigEntry Headers
	KeyHeader = "Key"

	// ValueHeader ConfigEntry Headers
	ValueHeader = "Value"
)

// ConfigEntryContext for configEntry outputs
type ConfigEntryContext struct {
	formatter.HeaderContext
	formatter.Context
	ce ybaclient.ConfigEntry
}

// NewConfigEntryFormat for formatting output
func NewConfigEntryFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultConfigEntry
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetConfigEntry initializes the context with the configEntry data
func (ce *ConfigEntryContext) SetConfigEntry(configEntry ybaclient.ConfigEntry) {
	ce.ce = configEntry
}

type configEntryContext struct {
	ConfigEntry *ConfigEntryContext
}

// Write populates the output table to be displayed in the command line
func (ce *ConfigEntryContext) Write(index int) error {
	var err error
	cec := &configEntryContext{
		ConfigEntry: &ConfigEntryContext{},
	}
	cec.ConfigEntry.ce = ce.ce

	// Section 1
	tmpl, err := ce.startSubsection(defaultConfigEntry)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ce.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Config Entry %d: Details", index+1), formatter.BlueColor)))
	ce.Output.Write([]byte("\n"))
	if err := ce.ContextFormat(tmpl, cec.ConfigEntry); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ce.PostFormat(tmpl, NewConfigEntryContext())
	ce.Output.Write([]byte("\n"))

	return nil
}

func (ce *ConfigEntryContext) startSubsection(format string) (*template.Template, error) {
	ce.Buffer = bytes.NewBufferString("")
	ce.ContextHeader = ""
	ce.Format = formatter.Format(format)
	ce.PreFormat()

	return ce.ParseFormat()
}

func (ce *ConfigEntryContext) subSection(name string) {
	ce.Output.Write([]byte("\n"))
	ce.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	ce.Output.Write([]byte("\n"))
}

// NewConfigEntryContext creates a new context for rendering configEntrys
func NewConfigEntryContext() *ConfigEntryContext {
	configEntryCtx := ConfigEntryContext{}
	configEntryCtx.Header = formatter.SubHeaderContext{
		"Inherited": inheritedHeader,
		"Key":       KeyHeader,
		"Value":     ValueHeader,
	}
	return &configEntryCtx
}

// Inherited function
func (ce *ConfigEntryContext) Inherited() string {
	return fmt.Sprintf("%t", ce.ce.GetInherited())
}

// Key function
func (ce *ConfigEntryContext) Key() string {
	return ce.ce.GetKey()
}

// Value function
func (ce *ConfigEntryContext) Value() string {
	return ce.ce.GetValue()
}

// MarshalJSON function
func (ce *ConfigEntryContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(ce.ce)
}
