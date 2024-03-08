/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"bytes"
	"encoding/json"
	"fmt"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/kubernetes"
)

const (
	subnetHeader          = "Subnet"
	secondarySubnetHeader = "Secondary Subnet"

	defaultZone = "table {{.Name}}\t{{.UUID}}\t{{.Subnet}}\t{{.AzRF}}\t{{.AzNumNodes}}"
)

// ZoneContext for zone outputs
type ZoneContext struct {
	formatter.HeaderContext
	formatter.Context
	z ybaclient.PlacementAZ
}

// NewZoneFormat for formatting output
func NewZoneFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultZone
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetZone initializes the context with the zone data
func (z *ZoneContext) SetZone(zone ybaclient.PlacementAZ) {
	z.z = zone
}

type zoneContext struct {
	Zone     *ZoneContext
	KubeZone *kubernetes.ZoneContext
}

// Write populates the output table to be displayed in the command line
func (z *ZoneContext) Write(index int) error {
	var err error
	zc := &zoneContext{
		Zone: &ZoneContext{},
	}
	zc.Zone.z = z.z

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
		"Name":       formatter.NameHeader,
		"UUID":       formatter.UUIDHeader,
		"Subnet":     subnetHeader,
		"AzRF":       rfHeader,
		"AzNumNodes": nodeHeader,
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

// Subnet fetches Zone Subnet
func (z *ZoneContext) Subnet() string {
	return z.z.GetSubnet()
}

// AzNumNodes fetches number of nodes in AZ
func (z *ZoneContext) AzNumNodes() string {
	return fmt.Sprintf("%d", z.z.GetNumNodesInAZ())
}

// AzRF fetches rf in AZ
func (z *ZoneContext) AzRF() string {
	return fmt.Sprintf("%d", z.z.GetReplicationFactor())
}

// MarshalJSON function
func (z *ZoneContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(z.z)
}
