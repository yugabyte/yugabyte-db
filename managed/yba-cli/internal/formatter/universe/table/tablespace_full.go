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

// FullTablespaceContext to render Table Details output
type FullTablespaceContext struct {
	formatter.HeaderContext
	formatter.Context
	t ybaclient.TableSpaceInfo
}

// SetFullTablespace initializes the context with the table data
func (ft *FullTablespaceContext) SetFullTablespace(table ybaclient.TableSpaceInfo) {
	ft.t = table
}

// NewFullTablespaceFormat for formatting output
func NewFullTablespaceFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTablespaceListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullTablespaceContext struct {
	Table *TablespaceContext
}

// Write populates the output table to be displayed in the command line
func (ft *FullTablespaceContext) Write() error {
	var err error
	ftc := &fullTablespaceContext{
		Table: &TablespaceContext{},
	}
	ftc.Table.t = ft.t

	// Section 1
	tmpl, err := ft.startSubsection(tablespaceList1)
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
	ft.PostFormat(tmpl, NewTablespaceContext())
	ft.Output.Write([]byte("\n"))

	// placement blocks
	logrus.Debugf("Number of Placement Blocks: %d", len(ft.t.GetPlacementBlocks()))
	ft.subSection("Placement Blocks")
	for i, v := range ft.t.GetPlacementBlocks() {
		placementBlockContext := *NewPlacementBlockContext()
		placementBlockContext.Output = os.Stdout
		placementBlockContext.Format = NewFullTablespaceFormat(viper.GetString("output"))
		placementBlockContext.SetPlacementBlock(v)
		placementBlockContext.Write(i)
	}

	return nil
}

func (ft *FullTablespaceContext) startSubsection(format string) (*template.Template, error) {
	ft.Buffer = bytes.NewBufferString("")
	ft.ContextHeader = ""
	ft.Format = formatter.Format(format)
	ft.PreFormat()

	return ft.ParseFormat()
}

func (ft *FullTablespaceContext) subSection(name string) {
	ft.Output.Write([]byte("\n"))
	ft.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	ft.Output.Write([]byte("\n"))
}

// NewFullTablespaceContext creates a new context for rendering table
func NewFullTablespaceContext() *FullTablespaceContext {
	tableCtx := FullTablespaceContext{}
	tableCtx.Header = formatter.SubHeaderContext{}
	return &tableCtx
}

// MarshalJSON function
func (ft *FullTablespaceContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(ft.t)
}
