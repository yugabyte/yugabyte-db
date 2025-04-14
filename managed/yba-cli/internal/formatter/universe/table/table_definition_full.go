/*
 * Copyright (c) YugaByte, Inc.
 */

package table

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

const ()

// FullDefinitionContext to render Table Details output
type FullDefinitionContext struct {
	formatter.HeaderContext
	formatter.Context
	t ybaclient.TableDefinitionTaskParams
}

// SetFullDefinition initializes the context with the table data
func (ft *FullDefinitionContext) SetFullDefinition(table ybaclient.TableDefinitionTaskParams) {
	ft.t = table
}

// NewFullDefinitionFormat for formatting output
func NewFullDefinitionFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTableDefinitionListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullDefinitionContext struct {
	Table *DefinitionContext
}

// Write populates the output table to be displayed in the command line
func (ft *FullDefinitionContext) Write() error {
	var err error
	ftc := &fullDefinitionContext{
		Table: &DefinitionContext{},
	}
	ftc.Table.t = ft.t

	// Section 1
	tmpl, err := ft.startSubsection(defaultTableDefinitionListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ft.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	ft.Output.Write([]byte("\n"))
	if err := ft.ContextFormat(tmpl, ftc.Table); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ft.PostFormat(tmpl, NewDefinitionContext())
	ft.Output.Write([]byte("\n"))

	// Section 2: Table Details subSection 1
	tmpl, err = ft.startSubsection(tableDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := ft.ContextFormat(tmpl, ftc.Table); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ft.PostFormat(tmpl, NewDefinitionContext())
	ft.Output.Write([]byte("\n"))

	tmpl, err = ft.startSubsection(tableDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := ft.ContextFormat(tmpl, ftc.Table); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ft.PostFormat(tmpl, NewDefinitionContext())
	ft.Output.Write([]byte("\n"))

	details := ft.t.GetTableDetails()
	// Columns subSection
	logrus.Debugf("Number of Columns: %d", len(details.GetColumns()))
	ft.subSection("Columns")
	for i, v := range details.GetColumns() {
		columnContext := *NewColumnContext()
		columnContext.Output = os.Stdout
		columnContext.Format = NewFullDefinitionFormat(viper.GetString("output"))
		columnContext.SetColumn(v)
		columnContext.Write(i)
	}

	return nil
}

func (ft *FullDefinitionContext) startSubsection(format string) (*template.Template, error) {
	ft.Buffer = bytes.NewBufferString("")
	ft.ContextHeader = ""
	ft.Format = formatter.Format(format)
	ft.PreFormat()

	return ft.ParseFormat()
}

func (ft *FullDefinitionContext) subSection(name string) {
	ft.Output.Write([]byte("\n"))
	ft.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	ft.Output.Write([]byte("\n"))
}

// NewFullDefinitionContext creates a new context for rendering table
func NewFullDefinitionContext() *FullDefinitionContext {
	tableCtx := FullDefinitionContext{}
	tableCtx.Header = formatter.SubHeaderContext{}
	return &tableCtx
}

// MarshalJSON function
func (ft *FullDefinitionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(ft.t)
}
