/*
 * Copyright (c) YugaByte, Inc.
 */

package scope

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

// FullScopeContext to render Scope Details output
type FullScopeContext struct {
	formatter.HeaderContext
	formatter.Context
	s ybaclient.ScopedConfig
}

// SetFullScope initializes the context with the scope data
func (fs *FullScopeContext) SetFullScope(scope ybaclient.ScopedConfig) {
	fs.s = scope
}

// NewFullScopeFormat for formatting output
func NewFullScopeFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultScopeListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullScopeContext struct {
	Scope *Context
}

// Write populates the output table to be displayed in the command line
func (fs *FullScopeContext) Write() error {
	var err error
	fpc := &fullScopeContext{
		Scope: &Context{},
	}
	fpc.Scope.s = fs.s

	// Section 1
	tmpl, err := fs.startSubsection(defaultScopeListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fs.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fs.Output.Write([]byte("\n"))
	if err := fs.ContextFormat(tmpl, fpc.Scope); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fs.PostFormat(tmpl, NewScopeContext())
	fs.Output.Write([]byte("\n"))

	if len(fs.s.GetConfigEntries()) != 0 {
		// configEntries subSection
		logrus.Debugf("Number of Config Entries: %d", len(fs.s.GetConfigEntries()))
		fs.subSection("Configuration Entries")
		for i, v := range fs.s.GetConfigEntries() {
			configEntryContext := *NewConfigEntryContext()
			configEntryContext.Output = os.Stdout
			configEntryContext.Format = NewFullScopeFormat(viper.GetString("output"))
			configEntryContext.SetConfigEntry(v)
			configEntryContext.Write(i)
		}
	}
	return nil
}

func (fs *FullScopeContext) startSubsection(format string) (*template.Template, error) {
	fs.Buffer = bytes.NewBufferString("")
	fs.ContextHeader = ""
	fs.Format = formatter.Format(format)
	fs.PreFormat()

	return fs.ParseFormat()
}

func (fs *FullScopeContext) subSection(name string) {
	fs.Output.Write([]byte("\n"))
	fs.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fs.Output.Write([]byte("\n"))
}

// NewFullScopeContext creates a new context for rendering scope
func NewFullScopeContext() *FullScopeContext {
	scopeCtx := FullScopeContext{}
	scopeCtx.Header = formatter.SubHeaderContext{}
	return &scopeCtx
}

// MarshalJSON function
func (fs *FullScopeContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fs.s)
}
