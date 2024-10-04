/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (

	// Region Details
	defaultRegion = "table {{.Name}}\t{{.Code}}\t{{.UUID}}\t{{.LoadBalancerFQDN}}"

	loadBalancerFQDNHeader = "Load Balancer FQDN"
)

// RegionContext for region outputs
type RegionContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.PlacementRegion
}

// NewRegionFormat for formatting output
func NewRegionFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultRegion
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetRegion initializes the context with the region data
func (r *RegionContext) SetRegion(region ybaclient.PlacementRegion) {
	r.r = region
}

type regionContext struct {
	Region *RegionContext
}

// Write populates the output table to be displayed in the command line
func (r *RegionContext) Write(index int) error {
	var err error
	rc := &regionContext{
		Region: &RegionContext{},
	}
	rc.Region.r = r.r

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

	// Zones Subsection
	logrus.Debugf("Number of Zones: %d", len(r.r.GetAzList()))
	r.subSection("Zones")
	for i, v := range r.r.GetAzList() {
		zoneContext := *NewZoneContext()
		zoneContext.Output = os.Stdout
		zoneContext.Format = NewFullUniverseFormat(viper.GetString("output"))
		zoneContext.SetZone(v)
		zoneContext.Write(i)
	}

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
		"LoadBalancerFQDN": loadBalancerFQDNHeader,
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

// LoadBalancerFQDN fetches FQDN
func (r *RegionContext) LoadBalancerFQDN() string {
	return r.r.GetLbFQDN()
}

// MarshalJSON function
func (r *RegionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(r.r)
}
