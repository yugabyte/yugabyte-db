/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/aws"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/azu"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/gcp"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/kubernetes"
)

const (

	// Region Details
	defaultRegion = "table {{.Name}}\t{{.Code}}\t{{.UUID}}\t{{.Latitude}}\t{{.Longitude}}"

	latitudeHeader         = "Latitude"
	longitudeHeader        = "Longitude"
	instanceTemplateHeader = "Instance Template"
)

// RegionContext for region outputs
type RegionContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.Region
}

// NewRegionFormat for formatting output
func NewRegionFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultRegion
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetRegion initializes the context with the region data
func (r *RegionContext) SetRegion(region ybaclient.Region) {
	r.r = region
}

type regionContext struct {
	Region      *RegionContext
	AWSRegion   *aws.RegionContext
	GCPRegion   *gcp.RegionContext
	AzureRegion *azu.RegionContext
	KubeRegion  *kubernetes.RegionContext
}

// Write populates the output table to be displayed in the command line
func (r *RegionContext) Write(providerCode string, index int) error {
	var err error
	rc := &regionContext{
		Region: &RegionContext{},
	}
	rc.Region.r = r.r
	details := r.r.GetDetails()
	cloudInfo := details.GetCloudInfo()
	rc.AWSRegion = &aws.RegionContext{
		Region: cloudInfo.GetAws(),
	}
	rc.KubeRegion = &kubernetes.RegionContext{
		Region: cloudInfo.GetKubernetes(),
	}
	rc.GCPRegion = &gcp.RegionContext{
		Region: cloudInfo.GetGcp(),
	}
	rc.AzureRegion = &azu.RegionContext{
		Region: cloudInfo.GetAzu(),
	}

	// Section 1
	tmpl, err := r.startSubsection(defaultRegion)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	r.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Region %d: Details", index+1), formatter.BlueColor)))
	r.Output.Write([]byte("\n"))
	if err := r.ContextFormat(tmpl, rc.Region); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	r.PostFormat(tmpl, NewRegionContext())

	switch providerCode {
	case util.AWSProviderType:
		tmpl, err = r.startSubsection(aws.Region)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.subSection("AWS Region Details")
		if err := r.ContextFormat(tmpl, rc.AWSRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, aws.NewRegionContext())
	case util.GCPProviderType:
		tmpl, err = r.startSubsection(gcp.Region)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.subSection("GCP Region Details")
		if err := r.ContextFormat(tmpl, rc.GCPRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, gcp.NewRegionContext())
	case util.AzureProviderType:
		tmpl, err = r.startSubsection(azu.Region)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.subSection("Azure Region Details")
		if err := r.ContextFormat(tmpl, rc.AzureRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, azu.NewRegionContext())
	case util.K8sProviderType:
		tmpl, err = r.startSubsection(kubernetes.Region1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.subSection("Kubernetes Region Details")
		if err := r.ContextFormat(tmpl, rc.KubeRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, kubernetes.NewRegionContext())
		r.Output.Write([]byte("\n"))

		tmpl, err = r.startSubsection(kubernetes.Region2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := r.ContextFormat(tmpl, rc.KubeRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, kubernetes.NewRegionContext())
		r.Output.Write([]byte("\n"))

		tmpl, err = r.startSubsection(kubernetes.Region3)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := r.ContextFormat(tmpl, rc.KubeRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, kubernetes.NewRegionContext())
		r.Output.Write([]byte("\n"))

		tmpl, err = r.startSubsection(kubernetes.Region4)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := r.ContextFormat(tmpl, rc.KubeRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, kubernetes.NewRegionContext())
		r.Output.Write([]byte("\n"))

		tmpl, err = r.startSubsection(kubernetes.Region5)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := r.ContextFormat(tmpl, rc.KubeRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, kubernetes.NewRegionContext())

		tmpl, err = r.startSubsection(kubernetes.Region6)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := r.ContextFormat(tmpl, rc.KubeRegion); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		r.PostFormat(tmpl, kubernetes.NewRegionContext())
	}

	// Zones Subsection
	logrus.Debugf("Number of Zones: %d", len(r.r.GetZones()))
	r.subSection("Zones")
	for i, v := range r.r.GetZones() {
		zoneContext := *NewZoneContext()
		zoneContext.Output = os.Stdout
		zoneContext.Format = NewFullProviderFormat(viper.GetString("output"))
		zoneContext.SetZone(v)
		zoneContext.Write(providerCode, i)
	}
	r.Output.Write([]byte("\n"))

	return nil
}

func (r *RegionContext) startSubsection(format string) (*template.Template, error) {
	r.Buffer = bytes.NewBufferString("")
	r.ContextHeader = ""
	r.Format = formatter.Format(format)
	r.PreFormat()

	return r.ParseFormat()
}

func (r *RegionContext) subSection(name string) {
	r.Output.Write([]byte("\n\n"))
	r.Output.Write([]byte(formatter.Colorize(name, formatter.BlueColor)))
	r.Output.Write([]byte("\n"))
}

// NewRegionContext creates a new context for rendering regions
func NewRegionContext() *RegionContext {
	regionCtx := RegionContext{}
	regionCtx.Header = formatter.SubHeaderContext{
		"Name":             formatter.NameHeader,
		"UUID":             formatter.UUIDHeader,
		"Code":             formatter.CodeHeader,
		"Latitude":         latitudeHeader,
		"Longitude":        longitudeHeader,
		"InstanceTemplate": instanceTemplateHeader,
	}
	return &regionCtx
}

// UUID fetches Region UUID
func (r *RegionContext) UUID() string {
	return r.r.GetUuid()
}

// Name fetches Region Name
func (r *RegionContext) Name() string {
	return r.r.GetName()
}

// Code fetches Region Code
func (r *RegionContext) Code() string {
	return r.r.GetCode()
}

// Latitude fetches Region Latitude
func (r *RegionContext) Latitude() string {
	return fmt.Sprintf("%f", r.r.GetLatitude())
}

// Longitude fetches Region Longitude
func (r *RegionContext) Longitude() string {
	return fmt.Sprintf("%f", r.r.GetLongitude())
}

// MarshalJSON function
func (r *RegionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(r.r)
}
