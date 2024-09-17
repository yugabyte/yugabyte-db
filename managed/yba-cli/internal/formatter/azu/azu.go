/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// Provider1 provides header for AZU Cloud Info
	Provider1 = "table {{.ClientID}}\t{{.ClientSecret}}\t{{.SubscriptionID}}\t{{.TenantID}}" +
		"\t{{.RG}}"
	// Provider2 provides header for AZU Cloud Info
	Provider2 = "table {{.HostedZoneID}}\t{{.NetworkSubscriptionID}}\t{{.NetworkRG}}" +
		"\t{{.VpcType}}"
	// Region provides header for AZU Region Cloud Info
	Region = "table {{.SecurityGroupID}}\t{{.VNet}}\t.{{.YbImage}}"

	// EAR1 provides header for Azure KMS Info
	EAR1 = "table {{.ClientID}}\t{{.ClientSecret}}\t{{.TenantID}}"

	// EAR2 provides header for Azure KMS Info
	EAR2 = "table {{.VaultURL}}\t{{.KeyName}}\t{{.KeyAlgorithm}}\t{{.KeySize}}"

	clientIDHeader              = "Azure Client ID"
	clientSecretHeader          = "Azure Client Secret"
	subscriptionIDHeader        = "Azure Subscription ID"
	tenantIDHeader              = "Azure Tenant ID"
	rgHeader                    = "Azure Resource Group"
	networkSubscriptionIDHeader = "Network Subscription ID"
	networkRGHeader             = "Network Resource Group"
	vpcTypeHeader               = "VPC Type"
	hostedZoneIDHeader          = "Hosted Zone ID"
	sgIDHeader                  = "Security Group ID"
	vnetHeader                  = "Virual Network"
	ybImageHeader               = "YB Image"

	keyAlgorithmHeader = "Key Algorithm"

	keySizeHeader = "Key Size"

	vaultURLHeader = "Vault URL"

	keyNameHeader = "Key Name"
)

// ProviderContext for provider outputs
type ProviderContext struct {
	formatter.HeaderContext
	formatter.Context
	Azu ybaclient.AzureCloudInfo
}

// RegionContext for provider outputs
type RegionContext struct {
	formatter.HeaderContext
	formatter.Context
	Region ybaclient.AzureRegionCloudInfo
}

// EARContext for kms outputs
type EARContext struct {
	formatter.HeaderContext
	formatter.Context
	Azu util.AzuKmsAuthConfigField
}

// NewProviderFormat for formatting output
func NewProviderFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := Provider1
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewRegionFormat for formatting output
func NewRegionFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := Region
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewEARFormat for formatting output
func NewEARFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := EAR1
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewProviderContext creates a new context for rendering provider
func NewProviderContext() *ProviderContext {
	azuProviderCtx := ProviderContext{}
	azuProviderCtx.Header = formatter.SubHeaderContext{
		"ClientID":              clientIDHeader,
		"ClientSecret":          clientSecretHeader,
		"SubscriptionID":        subscriptionIDHeader,
		"TenantID":              tenantIDHeader,
		"RG":                    rgHeader,
		"NetworkSubscriptionID": networkSubscriptionIDHeader,
		"NetworkRG":             networkRGHeader,
		"HostedZoneID":          hostedZoneIDHeader,
		"VpcType":               vpcTypeHeader,
	}
	return &azuProviderCtx
}

// NewRegionContext creates a new context for rendering provider
func NewRegionContext() *RegionContext {
	azuRegionCtx := RegionContext{}
	azuRegionCtx.Header = formatter.SubHeaderContext{
		"SecurityGroupID": sgIDHeader,
		"VNet":            vnetHeader,
		"YbImage":         ybImageHeader,
	}
	return &azuRegionCtx
}

// NewEARContext creates a new context for rendering ear config
func NewEARContext() *EARContext {
	azuEARCtx := EARContext{}
	azuEARCtx.Header = formatter.SubHeaderContext{
		"ClientID":     clientIDHeader,
		"ClientSecret": clientSecretHeader,
		"TenantID":     tenantIDHeader,
		"VaultURL":     vaultURLHeader,
		"KeyName":      keyNameHeader,
		"KeyAlgorithm": keyAlgorithmHeader,
		"KeySize":      keySizeHeader,
	}
	return &azuEARCtx
}

// ClientID fetches Azure Client ID
func (c *ProviderContext) ClientID() string {
	return c.Azu.GetAzuClientId()
}

// ClientSecret fetches Azure Client Secret
func (c *ProviderContext) ClientSecret() string {
	return c.Azu.GetAzuClientSecret()
}

// HostedZoneID fetches Azure Hosted Zone ID
func (c *ProviderContext) HostedZoneID() string {
	return c.Azu.GetAzuHostedZoneId()
}

// NetworkRG fetches Azure Network Resource Group
func (c *ProviderContext) NetworkRG() string {
	return c.Azu.GetAzuNetworkRG()
}

// NetworkSubscriptionID fetches Azure Network Subscription ID
func (c *ProviderContext) NetworkSubscriptionID() string {
	return c.Azu.GetAzuNetworkSubscriptionId()
}

// RG fetches Azure Resource Group
func (c *ProviderContext) RG() string {
	return c.Azu.GetAzuRG()
}

// SubscriptionID fetches Azure Subscription ID
func (c *ProviderContext) SubscriptionID() string {
	return c.Azu.GetAzuSubscriptionId()
}

// TenantID fetches Azure Tenant ID
func (c *ProviderContext) TenantID() string {
	return c.Azu.GetAzuTenantId()
}

// VpcType fetches VPC Type
func (c *ProviderContext) VpcType() string {
	return c.Azu.GetVpcType()
}

// MarshalJSON function
func (c *ProviderContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Azu)
}

// SecurityGroupID fetches Azure Region security group ID
func (c *RegionContext) SecurityGroupID() string {
	return c.Region.GetSecurityGroupId()
}

// VNet fetches Azure Region virtual network
func (c *RegionContext) VNet() string {
	return c.Region.GetVnet()
}

// YbImage fetches Azure Region yb image
func (c *RegionContext) YbImage() string {
	return c.Region.GetYbImage()
}

// MarshalJSON function
func (c *RegionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Region)
}

// ClientID fetches Azure Client ID
func (c *EARContext) ClientID() string {
	return c.Azu.ClientID
}

// ClientSecret fetches Azure Client Secret
func (c *EARContext) ClientSecret() string {
	return c.Azu.ClientSecret
}

// TenantID fetches Azure Tenant ID
func (c *EARContext) TenantID() string {
	return c.Azu.TenantID
}

// VaultURL fetches Azure Vault URL
func (c *EARContext) VaultURL() string {
	return c.Azu.AzuVaultURL
}

// KeyName fetches Azure Key Name
func (c *EARContext) KeyName() string {
	return c.Azu.AzuKeyName
}

// KeyAlgorithm fetches Azure Key Algorithm
func (c *EARContext) KeyAlgorithm() string {
	return c.Azu.AzuKeyAlgorithm
}

// KeySize fetches Azure Key Size
func (c *EARContext) KeySize() int {
	return c.Azu.AzuKeySize
}

// MarshalJSON function
func (c *EARContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Azu)
}
