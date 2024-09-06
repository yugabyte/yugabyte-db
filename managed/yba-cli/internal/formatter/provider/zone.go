/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"bytes"
	"encoding/json"
	"fmt"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/kubernetes"
)

const (
	subnetHeader          = "Subnet"
	secondarySubnetHeader = "Secondary Subnet"

	defaultZone = "table {{.Name}}\t{{.Code}}\t{{.UUID}}\t{{.Subnet}}\t{{.SecondarySubnet}}"
)

// ZoneContext for zone outputs
type ZoneContext struct {
	formatter.HeaderContext
	formatter.Context
	z ybaclient.AvailabilityZone
}

// NewZoneFormat for formatting output
func NewZoneFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultZone
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetZone initializes the context with the zone data
func (z *ZoneContext) SetZone(zone ybaclient.AvailabilityZone) {
	z.z = zone
}

type zoneContext struct {
	Zone     *ZoneContext
	KubeZone *kubernetes.ZoneContext
}

// Write populates the output table to be displayed in the command line
func (z *ZoneContext) Write(providerCode string, index int) error {
	var err error
	zc := &zoneContext{
		Zone: &ZoneContext{},
	}
	zc.Zone.z = z.z
	details := z.z.GetDetails()
	cloudInfo := details.GetCloudInfo()
	zc.KubeZone = &kubernetes.ZoneContext{
		Zone: cloudInfo.GetKubernetes(),
	}

	// Section 1
	tmpl, err := z.startSubsection(defaultZone)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	z.Output.Write([]byte(formatter.Colorize(fmt.Sprintf(
		"Zone %d: Details", index+1), formatter.RedColor)))
	z.Output.Write([]byte("\n"))
	if err := z.ContextFormat(tmpl, zc.Zone); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	z.PostFormat(tmpl, NewZoneContext())
	switch providerCode {
	case util.K8sProviderType:
		tmpl, err = z.startSubsection(kubernetes.Region1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		z.subSection("Kubernetes Zone Details")
		if err := z.ContextFormat(tmpl, zc.KubeZone); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		z.PostFormat(tmpl, kubernetes.NewZoneContext())
		z.Output.Write([]byte("\n"))

		tmpl, err = z.startSubsection(kubernetes.Region2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := z.ContextFormat(tmpl, zc.KubeZone); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		z.PostFormat(tmpl, kubernetes.NewZoneContext())
		z.Output.Write([]byte("\n"))

		tmpl, err = z.startSubsection(kubernetes.Region3)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := z.ContextFormat(tmpl, zc.KubeZone); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		z.PostFormat(tmpl, kubernetes.NewZoneContext())
		z.Output.Write([]byte("\n"))

		tmpl, err = z.startSubsection(kubernetes.Region4)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := z.ContextFormat(tmpl, zc.KubeZone); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		z.PostFormat(tmpl, kubernetes.NewZoneContext())
		z.Output.Write([]byte("\n"))

		tmpl, err = z.startSubsection(kubernetes.Region5)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := z.ContextFormat(tmpl, zc.KubeZone); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		z.PostFormat(tmpl, kubernetes.NewZoneContext())

		tmpl, err = z.startSubsection(kubernetes.Region6)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := z.ContextFormat(tmpl, zc.KubeZone); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		z.PostFormat(tmpl, kubernetes.NewZoneContext())

	}
	z.Output.Write([]byte("\n"))
	return nil
}

func (z *ZoneContext) startSubsection(format string) (*template.Template, error) {
	z.Buffer = bytes.NewBufferString("")
	z.ContextHeader = ""
	z.Format = formatter.Format(format)
	z.PreFormat()

	return z.ParseFormat()
}

func (z *ZoneContext) subSection(name string) {
	z.Output.Write([]byte("\n\n"))
	z.Output.Write([]byte(formatter.Colorize(name, formatter.RedColor)))
	z.Output.Write([]byte("\n"))
}

// NewZoneContext creates a new context for rendering zones
func NewZoneContext() *ZoneContext {
	zoneCtx := ZoneContext{}
	zoneCtx.Header = formatter.SubHeaderContext{
		"Name":            formatter.NameHeader,
		"UUID":            formatter.UUIDHeader,
		"Code":            formatter.CodeHeader,
		"Subnet":          subnetHeader,
		"SecondarySubnet": secondarySubnetHeader,
	}
	return &zoneCtx
}

// UUID fetches Zone UUID
func (z *ZoneContext) UUID() string {
	return z.z.GetUuid()
}

// Name fetches Zone Name
func (z *ZoneContext) Name() string {
	return z.z.GetName()
}

// Code fetches Zone Code
func (z *ZoneContext) Code() string {
	return z.z.GetCode()
}

// Subnet fetches Zone Subnet
func (z *ZoneContext) Subnet() string {
	return z.z.GetSubnet()
}

// SecondarySubnet fetches Zone SecondarySubnet
func (z *ZoneContext) SecondarySubnet() string {
	return z.z.GetSecondarySubnet()
}

// MarshalJSON function
func (z *ZoneContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(z.z)
}
