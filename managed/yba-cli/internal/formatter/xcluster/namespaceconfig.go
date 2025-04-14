/*
 * Copyright (c) YugaByte, Inc.
 */

package xcluster

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
	defaultNamespaceConfig  = "table {{.SourceNamespaceID}}\t{{.Status}}"
	namespaceInfoTable      = "table {{.Name}}\t{{.NamespaceUUID}}\t{{.TableType}}"
	sourceNamespaceIDHeader = "Source Namespace ID"
)

// NamespaceConfigContext for namespaceConfig outputs
type NamespaceConfigContext struct {
	formatter.HeaderContext
	formatter.Context
	n ybaclient.XClusterNamespaceConfig
}

// NamespaceInfoContext for namespaceConfig outputs
type NamespaceInfoContext struct {
	formatter.HeaderContext
	formatter.Context
	n ybaclient.NamespaceInfoResp
}

// NewNamespaceConfigFormat for formatting output
func NewNamespaceConfigFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultNamespaceConfig
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewNamespaceInfoFormat for formatting output
func NewNamespaceInfoFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := namespaceInfoTable
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetNamespaceConfig initializes the context with the namespaceConfig data
func (n *NamespaceConfigContext) SetNamespaceConfig(
	namespaceConfig ybaclient.XClusterNamespaceConfig,
) {
	n.n = namespaceConfig
}

// SetNamespaceInfo initializes the context with the namespaceConfig data
func (n *NamespaceInfoContext) SetNamespaceInfo(namespaceInfo ybaclient.NamespaceInfoResp) {
	n.n = namespaceInfo
}

type namespaceConfigContext struct {
	NamespaceConfig     *NamespaceConfigContext
	SourceNamespaceInfo *NamespaceInfoContext
	TargetNamespaceInfo *NamespaceInfoContext
}

// Write populates the output table to be displayed in the command line
func (n *NamespaceConfigContext) Write(index int) error {
	var err error
	nc := &namespaceConfigContext{
		NamespaceConfig: &NamespaceConfigContext{},
	}
	nc.NamespaceConfig.n = n.n

	nc.SourceNamespaceInfo = &NamespaceInfoContext{
		n: n.n.GetSourceNamespaceInfo(),
	}

	nc.TargetNamespaceInfo = &NamespaceInfoContext{
		n: n.n.GetTargetNamespaceInfo(),
	}

	// Section 1
	tmpl, err := n.startSubsection(defaultNamespaceConfig)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	n.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Namespace Configuration %d: Details", index+1), formatter.BlueColor)))
	n.Output.Write([]byte("\n"))
	if err := n.ContextFormat(tmpl, nc.NamespaceConfig); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	n.PostFormat(tmpl, NewNamespaceConfigContext())
	n.Output.Write([]byte("\n"))

	tmpl, err = n.startSubsection(namespaceInfoTable)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	n.subSection("Source Namespace Info")
	if err := n.ContextFormat(tmpl, nc.SourceNamespaceInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	n.PostFormat(tmpl, NewNamespaceConfigContext())

	tmpl, err = n.startSubsection(namespaceInfoTable)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	n.subSection("Target Namespace Info")
	if err := n.ContextFormat(tmpl, nc.TargetNamespaceInfo); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	n.PostFormat(tmpl, NewNamespaceConfigContext())

	return nil
}

func (n *NamespaceConfigContext) startSubsection(format string) (*template.Template, error) {
	n.Buffer = bytes.NewBufferString("")
	n.ContextHeader = ""
	n.Format = formatter.Format(format)
	n.PreFormat()

	return n.ParseFormat()
}

func (n *NamespaceConfigContext) subSection(name string) {
	n.Output.Write([]byte("\n"))
	n.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	n.Output.Write([]byte("\n"))
}

// NewNamespaceConfigContext creates a new context for rendering namespaceConfigs
func NewNamespaceConfigContext() *NamespaceConfigContext {
	namespaceConfigCtx := NamespaceConfigContext{}
	namespaceConfigCtx.Header = formatter.SubHeaderContext{
		"SourceNamespaceID": sourceNamespaceIDHeader,
		"Status":            formatter.StatusHeader,
	}
	return &namespaceConfigCtx
}

// NewNamespaceInfoContext creates a new context for rendering namespaceConfigs
func NewNamespaceInfoContext() *NamespaceInfoContext {
	namespaceInfoCtx := NamespaceInfoContext{}
	namespaceInfoCtx.Header = formatter.SubHeaderContext{
		"Name":          formatter.NameHeader,
		"NamespaceUUID": formatter.UUIDHeader,
		"TableType":     tableTypeHeader,
	}
	return &namespaceInfoCtx
}

// SourceNamespaceID fetches NamespaceConfig SourceNamespaceID
func (n *NamespaceConfigContext) SourceNamespaceID() string {
	return n.n.GetSourceNamespaceId()
}

// Status fetches NamespaceConfig Status
func (n *NamespaceConfigContext) Status() string {
	return n.n.GetStatus()
}

// MarshalJSON function
func (n *NamespaceConfigContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(n.n)
}

// Name fetches NamespaceConfig Name
func (n *NamespaceInfoContext) Name() string {
	return n.n.GetName()
}

// NamespaceUUID fetches NamespaceConfig NamespaceUUID
func (n *NamespaceInfoContext) NamespaceUUID() string {
	return n.n.GetNamespaceUUID()
}

// TableType fetches NamespaceConfig TableType
func (n *NamespaceInfoContext) TableType() string {
	return n.n.GetTableType()
}

// MarshalJSON function
func (n *NamespaceInfoContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(n.n)
}
