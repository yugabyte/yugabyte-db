/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetypes

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

const (
	defaultFullInstanceTypesGeneral = "table {{.Name}}\t{{.ProviderUUID}}"
	details                         = "table {{.Cores}}\t{{.Memory}}"
)

// FullInstanceTypesContext to render InstanceTypes Details output
type FullInstanceTypesContext struct {
	formatter.HeaderContext
	formatter.Context
	iT ybaclient.InstanceTypeResp
}

// SetFullInstanceTypes initializes the context with the provider data
func (fiT *FullInstanceTypesContext) SetFullInstanceTypes(provider ybaclient.InstanceTypeResp) {
	fiT.iT = provider
}

// NewFullInstanceTypesFormat for formatting output
func NewFullInstanceTypesFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultInstanceTypesListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullInstanceTypesContext struct {
	InstanceTypes *Context
}

// Write populates the output table to be displayed in the command line
func (fiT *FullInstanceTypesContext) Write() error {
	var err error
	fpc := &fullInstanceTypesContext{
		InstanceTypes: &Context{},
	}
	fpc.InstanceTypes.iT = fiT.iT

	// Section 1
	tmpl, err := fiT.startSubsection(defaultFullInstanceTypesGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fiT.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fiT.Output.Write([]byte("\n"))
	if err := fiT.ContextFormat(tmpl, fpc.InstanceTypes); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fiT.PostFormat(tmpl, NewInstanceTypesContext())
	fiT.Output.Write([]byte("\n"))

	// Section 2: InstanceTypes Details subSection 1
	tmpl, err = fiT.startSubsection(details)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fiT.subSection("Instance Type Details")
	if err := fiT.ContextFormat(tmpl, fpc.InstanceTypes); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fiT.PostFormat(tmpl, NewInstanceTypesContext())
	fiT.Output.Write([]byte("\n"))

	// Volume subSection
	instanceTypeDetails := fiT.iT.GetInstanceTypeDetails()

	logrus.Debugf("Number of Volumes: %d", len(instanceTypeDetails.GetVolumeDetailsList()))
	fiT.subSection("Volumes")
	for i, v := range instanceTypeDetails.GetVolumeDetailsList() {
		volumeContext := *NewVolumeContext()
		volumeContext.Output = os.Stdout
		volumeContext.Format = NewFullInstanceTypesFormat(viper.GetString("output"))
		volumeContext.SetVolume(v)
		volumeContext.Write(i)
	}

	return nil
}

func (fiT *FullInstanceTypesContext) startSubsection(format string) (*template.Template, error) {
	fiT.Buffer = bytes.NewBufferString("")
	fiT.ContextHeader = ""
	fiT.Format = formatter.Format(format)
	fiT.PreFormat()

	return fiT.ParseFormat()
}

func (fiT *FullInstanceTypesContext) subSection(name string) {
	fiT.Output.Write([]byte("\n\n"))
	fiT.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fiT.Output.Write([]byte("\n"))
}

// NewFullInstanceTypesContext creates a new context for rendering provider
func NewFullInstanceTypesContext() *FullInstanceTypesContext {
	providerCtx := FullInstanceTypesContext{}
	providerCtx.Header = formatter.SubHeaderContext{}
	return &providerCtx
}

// MarshalJSON function
func (fiT *FullInstanceTypesContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fiT.iT)
}
