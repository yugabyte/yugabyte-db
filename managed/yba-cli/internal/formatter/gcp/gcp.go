/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// Provider provides header for GCP Cloud Info
	Provider = "table {{.Project}}\t{{.VpcType}}" +
		"\t{{.FirewallTags}}"

	// Region provides header for GCP Region Cloud Info
	Region = "table {{.InstanceTemplate}}\t.{{.YbImage}}"

	projectHeader          = "GCE Project"
	vpcTypeHeader          = "VPC Type"
	ybFirewallTagsHeader   = "YB Firewall Tags"
	instanceTemplateHeader = "Instance Template"
	ybImageHeader          = "YB Image"
)

// ProviderContext for provider outputs
type ProviderContext struct {
	formatter.HeaderContext
	formatter.Context
	Gcp ybaclient.GCPCloudInfo
}

// RegionContext for provider outputs
type RegionContext struct {
	formatter.HeaderContext
	formatter.Context
	Region ybaclient.GCPRegionCloudInfo
}

// NewProviderFormat for formatting output
func NewProviderFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := Provider
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewRegionFormat for formatting output
func NewRegionFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := Region
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewProviderContext creates a new context for rendering provider
func NewProviderContext() *ProviderContext {
	gcpProviderCtx := ProviderContext{}
	gcpProviderCtx.Header = formatter.SubHeaderContext{

		"Project": projectHeader,

		"VpcType":      vpcTypeHeader,
		"FirewallTags": ybFirewallTagsHeader,
	}
	return &gcpProviderCtx
}

// NewRegionContext creates a new context for rendering provider
func NewRegionContext() *RegionContext {
	gcpRegionCtx := RegionContext{}
	gcpRegionCtx.Header = formatter.SubHeaderContext{
		"InstanceTemplate": instanceTemplateHeader,
		"YbImage":          ybImageHeader,
	}
	return &gcpRegionCtx
}

// Project fetches the GCE project
func (c *ProviderContext) Project() string {
	return c.Gcp.GetGceProject()
}

// VpcType fetches the VPC type
func (c *ProviderContext) VpcType() string {
	return c.Gcp.GetVpcType()
}

// FirewallTags fetches the YB firewall tags
func (c *ProviderContext) FirewallTags() string {
	return c.Gcp.GetYbFirewallTags()
}

// MarshalJSON function
func (c *ProviderContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Gcp)
}

// InstanceTemplate fetches the instance template
func (c *RegionContext) InstanceTemplate() string {
	return c.Region.GetInstanceTemplate()
}

// YbImage fetches the YB image
func (c *RegionContext) YbImage() string {
	return c.Region.GetYbImage()
}

// MarshalJSON function
func (c *RegionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Region)
}
