/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// Provider provides header for AWS Cloud Info
	Provider = "table {{.AccessKeyID}}\t{{.AccessKeySecret}}\t{{.HostedZoneID}}" +
		"\t{{.HostedZoneName}}\t{{.HostVpcID}}\t{{.HostVpcRegion}}\t{{.VpcType}}"

	// Region provides header for AWS Region Cloud Info
	Region = "table {{.Arch}}\t{{.SecurityGroupID}}\t{{.VNet}}\t{{.YbImage}}"

	accessKeyIDHeader     = "AWS Access Key ID"
	accessKeySecretHeader = "AWS Access Key Secret"
	hostedZoneIDHeader    = "Hosted Zone ID"
	hostedZoneNameHeader  = "Hosted Zone Name"
	hostVpcIDHeader       = "Host VPC ID"
	hostVpcRegionHeader   = "Host VPC Region"
	vpcTypeHeader         = "VPC Type"
	archHeader            = "Arch"
	sgIDHeader            = "Security Group ID"
	vnetHeader            = "Virual Network"
	ybImageHeader         = "YB Image"
)

// ProviderContext for provider outputs
type ProviderContext struct {
	formatter.HeaderContext
	formatter.Context
	Aws ybaclient.AWSCloudInfo
}

// RegionContext for provider outputs
type RegionContext struct {
	formatter.HeaderContext
	formatter.Context
	Region ybaclient.AWSRegionCloudInfo
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
	awsProviderCtx := ProviderContext{}
	awsProviderCtx.Header = formatter.SubHeaderContext{
		"AccessKeyID":     accessKeyIDHeader,
		"AccessKeySecret": accessKeySecretHeader,
		"HostedZoneID":    hostedZoneIDHeader,
		"HostedZoneName":  hostedZoneNameHeader,
		"HostVpcID":       hostVpcIDHeader,
		"HostVpcRegion":   hostVpcRegionHeader,
		"VpcType":         vpcTypeHeader,
	}
	return &awsProviderCtx
}

// NewRegionContext creates a new context for rendering provider
func NewRegionContext() *RegionContext {
	awsRegionCtx := RegionContext{}
	awsRegionCtx.Header = formatter.SubHeaderContext{
		"Arch":            archHeader,
		"SecurityGroupID": sgIDHeader,
		"VNet":            vnetHeader,
		"YbImage":         ybImageHeader,
	}
	return &awsRegionCtx
}

// AccessKeyID fetches AWS access key ID
func (c *ProviderContext) AccessKeyID() string {
	return c.Aws.GetAwsAccessKeyID()
}

// AccessKeySecret fetches AWS access key secret
func (c *ProviderContext) AccessKeySecret() string {
	return c.Aws.GetAwsAccessKeySecret()
}

// HostedZoneID fetches AWS Hosted Zone ID
func (c *ProviderContext) HostedZoneID() string {
	return c.Aws.GetAwsHostedZoneId()
}

// HostedZoneName fetches AWS Hosted Zone Name
func (c *ProviderContext) HostedZoneName() string {
	return c.Aws.GetAwsHostedZoneName()
}

// HostVpcID fetches AWS Host VPC ID
func (c *ProviderContext) HostVpcID() string {
	return c.Aws.GetHostVpcId()
}

// HostVpcRegion fetches AWS Host VPC Region Name
func (c *ProviderContext) HostVpcRegion() string {
	return c.Aws.GetHostVpcRegion()
}

// VpcType fetches AWS VPC Type
func (c *ProviderContext) VpcType() string {
	return c.Aws.GetVpcType()
}

// MarshalJSON function
func (c *ProviderContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Aws)
}

// Arch fetches AWS Region architecture type
func (c *RegionContext) Arch() string {
	return c.Region.GetArch()
}

// SecurityGroupID fetches AWS Region security group ID
func (c *RegionContext) SecurityGroupID() string {
	return c.Region.GetSecurityGroupId()
}

// VNet fetches AWS Region virtual network
func (c *RegionContext) VNet() string {
	return c.Region.GetVnet()
}

// YbImage fetches AWS Region yb image
func (c *RegionContext) YbImage() string {
	return c.Region.GetYbImage()
}

// MarshalJSON function
func (c *RegionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Region)
}
