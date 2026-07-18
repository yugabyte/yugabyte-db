/*
 * Copyright (c) YugabyteDB, Inc.
 */

package xcluster

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/pitr"
)

const (
	// XCluster details
	xClusterDetails2 = "table {{.Type}}\t{{.TableType}}\t{{.ReplicationGroupName}}" +
		"\t{{.CreateTime}}\t{{.ModifyTime}}"
	xClusterDetails1 = "table {{.SourceActive}}\t{{.SourceState}}\t{{.TargetActive}}" +
		"\t{{.TargetState}}\t{{.UsedForDR}}"
	xClusterDetails4 = "table {{.Dbs}}"
	xClusterDetails5 = "table {{.Lag}}"
)

// FullXClusterContext to render XCluster Details output
type FullXClusterContext struct {
	formatter.HeaderContext
	formatter.Context
	x ybaclient.XClusterConfigGetResp
}

// SetFullXCluster initializes the context with the xCluster data
func (fx *FullXClusterContext) SetFullXCluster(xCluster ybaclient.XClusterConfigGetResp) {
	fx.x = xCluster
}

// NewFullXClusterFormat for formatting output
func NewFullXClusterFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultXClusterListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullXClusterContext struct {
	XCluster *Context
}

// Write populates the output table to be displayed in the command line
func (fx *FullXClusterContext) Write() error {
	var err error
	fxc := &fullXClusterContext{
		XCluster: &Context{},
	}
	fxc.XCluster.x = fx.x

	// Section 1
	tmpl, err := fx.startSubsection(defaultXClusterListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fx.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fx.Output.Write([]byte("\n"))
	if err := fx.ContextFormat(tmpl, fxc.XCluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fx.PostFormat(tmpl, NewXClusterContext())
	fx.Output.Write([]byte("\n"))

	// Section 2: XCluster Details subSection 1
	tmpl, err = fx.startSubsection(xClusterDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fx.subSection("XCluster Details")
	if err := fx.ContextFormat(tmpl, fxc.XCluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fx.PostFormat(tmpl, NewXClusterContext())
	fx.Output.Write([]byte("\n"))

	// Section 2: XCluster Details subSection 2
	tmpl, err = fx.startSubsection(xClusterDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fx.ContextFormat(tmpl, fxc.XCluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fx.PostFormat(tmpl, NewXClusterContext())
	fx.Output.Write([]byte("\n"))

	tmpl, err = fx.startSubsection(xClusterDetails5)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fx.ContextFormat(tmpl, fxc.XCluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fx.PostFormat(tmpl, NewXClusterContext())
	fx.Output.Write([]byte("\n"))

	tmpl, err = fx.startSubsection(xClusterDetails4)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fx.ContextFormat(tmpl, fxc.XCluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fx.PostFormat(tmpl, NewXClusterContext())
	fx.Output.Write([]byte("\n"))

	// TableDetailss subSection
	logrus.Debugf("Number of Tables: %d", len(fx.x.GetTableDetails()))
	fx.subSection("Tables")
	for i, v := range fx.x.GetTableDetails() {
		tableDetailsContext := *NewTableDetailContext()
		tableDetailsContext.Output = os.Stdout
		tableDetailsContext.Format = NewFullXClusterFormat(viper.GetString("output"))
		tableDetailsContext.SetTableDetail(v)
		tableDetailsContext.Write(i)
	}

	// PITR Configurations
	logrus.Debugf("Number of PITR Configs: %d", len(fx.x.GetPitrConfigs()))
	if len(fx.x.GetPitrConfigs()) > 0 {
		fx.subSection("PITR Configurations")
		for i, v := range fx.x.GetPitrConfigs() {
			pitrContext := *pitr.NewFullPITRContext()
			pitrContext.Output = os.Stdout
			pitrContext.Format = NewFullXClusterFormat(viper.GetString("output"))
			pitrContext.SetFullPITR(v)
			pitrContext.Write(i)
		}
	}

	// Namespace Configurations
	logrus.Debugf("Number of Namespace Configs: %d", len(fx.x.GetNamespaceDetails()))
	if len(fx.x.GetNamespaceDetails()) > 0 {
		fx.subSection("Namespace Configurations")
		for i, v := range fx.x.GetNamespaceDetails() {
			namespaceContext := *NewNamespaceConfigContext()
			namespaceContext.Output = os.Stdout
			namespaceContext.Format = NewFullXClusterFormat(viper.GetString("output"))
			namespaceContext.SetNamespaceConfig(v)
			namespaceContext.Write(i)
		}
	}

	return nil
}

func (fx *FullXClusterContext) startSubsection(format string) (*template.Template, error) {
	fx.Buffer = bytes.NewBufferString("")
	fx.ContextHeader = ""
	fx.Format = formatter.Format(format)
	fx.PreFormat()

	return fx.ParseFormat()
}

func (fx *FullXClusterContext) subSection(name string) {
	fx.Output.Write([]byte("\n"))
	fx.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fx.Output.Write([]byte("\n"))
}

// NewFullXClusterContext creates a new context for rendering xCluster
func NewFullXClusterContext() *FullXClusterContext {
	xClusterCtx := FullXClusterContext{}
	xClusterCtx.Header = formatter.SubHeaderContext{}
	return &xClusterCtx
}

// MarshalJSON function
func (fx *FullXClusterContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fx.x)
}
