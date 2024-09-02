/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// Provider provides header for AWS Cloud Info
	Provider = "table {{.AccessKeyID}}\t{{.AccessKeySecret}}\t{{.HostedZoneID}}" +
		"\t{{.HostedZoneName}}\t{{.VpcType}}"

	// Region provides header for AWS Region Cloud Info
	Region = "table {{.Arch}}\t{{.SecurityGroupID}}\t{{.VNet}}\t{{.YbImage}}"

	// EAR1 for EAR listing
	EAR1 = "table {{.AccessKeyID}}\t{{.AccessKeySecret}}\t{{.EndPoint}}"

	// EAR2 for EAR listing
	EAR2 = "table {{.Region}}\t{{.CMKPolicy}}\t{{.CMKID}}"

	// AccessKeyIDHeader for Access key ID header
	AccessKeyIDHeader = "AWS Access Key ID"
	// AccessKeySecretHeader for Access key secret header
	AccessKeySecretHeader = "AWS Access Key Secret"

	hostedZoneIDHeader   = "Hosted Zone ID"
	hostedZoneNameHeader = "Hosted Zone Name"
	vpcTypeHeader        = "VPC Type"
	archHeader           = "Arch"
	sgIDHeader           = "Security Group ID"
	vnetHeader           = "Virual Network"
	ybImageHeader        = "YB Image"
	endPointHeader       = "EndPoint"
	cmkPolicyHeader      = "CMK Policy"
	cmkIDHeader          = "CMK ID"
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

// EARContext for ksm outputs
type EARContext struct {
	formatter.HeaderContext
	formatter.Context
	Aws util.AwsKmsAuthConfigField
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

// NewEARFormat for formatting output
func NewEARFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := EAR1
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewProviderContext creates a new context for rendering provider
func NewProviderContext() *ProviderContext {
	awsProviderCtx := ProviderContext{}
	awsProviderCtx.Header = formatter.SubHeaderContext{
		"AccessKeyID":     AccessKeyIDHeader,
		"AccessKeySecret": AccessKeySecretHeader,
		"HostedZoneID":    hostedZoneIDHeader,
		"HostedZoneName":  hostedZoneNameHeader,
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

// NewEARContext creates a new context for rendering kms config
func NewEARContext() *EARContext {
	awsEARCtx := EARContext{}
	awsEARCtx.Header = formatter.SubHeaderContext{
		"EndPoint":        endPointHeader,
		"CMKPolicy":       cmkPolicyHeader,
		"CMKID":           cmkIDHeader,
		"AccessKeyID":     AccessKeyIDHeader,
		"AccessKeySecret": AccessKeySecretHeader,
		"Region":          formatter.RegionsHeader,
	}
	return &awsEARCtx
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

// EndPoint fetches AWS end point
func (c *EARContext) EndPoint() string {
	return c.Aws.EndPoint
}

// CMKPolicy fetches AWS cmk policy
func (c *EARContext) CMKPolicy() string {
	return c.Aws.CMKPolicy
}

// CMKID fetches AWS cmk ID
func (c *EARContext) CMKID() string {
	return c.Aws.CMKID
}

// AccessKeyID fetches AWS access key ID
func (c *EARContext) AccessKeyID() string {
	return c.Aws.AccessKeyID
}

// AccessKeySecret fetches AWS access key secret
func (c *EARContext) AccessKeySecret() string {
	return c.Aws.SecretAccessKey
}

// Region fetches AWS region
func (c *EARContext) Region() string {
	return c.Aws.Region
}

// MarshalJSON function
func (c *EARContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Aws)
}
