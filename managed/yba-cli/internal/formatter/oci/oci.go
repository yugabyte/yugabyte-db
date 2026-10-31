/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// EAR1 lists OCI KMS identity fields
	EAR1 = "table {{.AuthType}}\t{{.Region}}\t{{.VaultID}}\t{{.CompartmentID}}"

	// EAR2 lists the API signing key fields. The private key itself is omitted.
	EAR2 = "table {{.TenancyID}}\t{{.UserID}}\t{{.Fingerprint}}\t{{.PrivateKey}}"

	// EAR3 lists the key name and resolved OCID
	EAR3 = "table {{.KeyName}}\t{{.KeyOCID}}"

	earAuthTypeHeader      = "Auth Type"
	earRegionHeader        = "Region"
	earVaultIDHeader       = "Vault OCID"
	earCompartmentIDHeader = "Compartment OCID"
	earTenancyIDHeader     = "Tenancy OCID"
	earUserIDHeader        = "User OCID"
	earFingerprintHeader   = "Fingerprint"
	earPrivateKeyHeader    = "Private Key"
	earKeyNameHeader       = "Key Name"
	earKeyOCIDHeader       = "Key OCID"

	// Provider1 provides header for OCI Cloud Info
	Provider1 = "table {{.AuthType}}\t{{.TenancyID}}\t{{.UserID}}\t{{.Fingerprint}}\t{{.CompartmentID}}"
	// Provider2 provides header for OCI Cloud Info
	Provider2 = "table {{.Region}}\t{{.HostedZoneID}}\t{{.VpcType}}"

	// Region provides header for OCI Region Cloud Info
	Region = "table {{.VNet}}\t{{.InstanceTemplate}}"

	providerAuthTypeHeader         = "OCI Auth Type"
	providerTenancyIDHeader        = "OCI Tenancy ID"
	providerUserIDHeader           = "OCI User ID"
	providerFingerprintHeader      = "OCI Fingerprint"
	providerCompartmentIDHeader    = "OCI Compartment ID"
	providerRegionHeader           = "OCI Region"
	providerHostedZoneIDHeader     = "Hosted Zone ID"
	providerVpcTypeHeader          = "VPC Type"
	providerVnetHeader             = "Virtual Network"
	providerInstanceTemplateHeader = "Instance Template"
)

// EARContext for OCI KMS outputs
type EARContext struct {
	formatter.HeaderContext
	formatter.Context
	Oci util.OciKmsAuthConfigField
}

// NewEARContext creates a new context for rendering an OCI KMS config
func NewEARContext() *EARContext {
	ociEARCtx := EARContext{}
	ociEARCtx.Header = formatter.SubHeaderContext{
		"AuthType":      earAuthTypeHeader,
		"Region":        earRegionHeader,
		"VaultID":       earVaultIDHeader,
		"CompartmentID": earCompartmentIDHeader,
		"TenancyID":     earTenancyIDHeader,
		"UserID":        earUserIDHeader,
		"Fingerprint":   earFingerprintHeader,
		"PrivateKey":    earPrivateKeyHeader,
		"KeyName":       earKeyNameHeader,
		"KeyOCID":       earKeyOCIDHeader,
	}
	return &ociEARCtx
}

// AuthType returns the OCI auth type
func (c *EARContext) AuthType() string { return c.Oci.AuthType }

// Region returns the OCI region
func (c *EARContext) Region() string { return c.Oci.Region }

// VaultID returns the vault OCID
func (c *EARContext) VaultID() string { return c.Oci.VaultID }

// CompartmentID returns the compartment OCID
func (c *EARContext) CompartmentID() string { return c.Oci.CompartmentID }

// TenancyID returns the tenancy OCID
func (c *EARContext) TenancyID() string { return c.Oci.TenancyID }

// UserID returns the user OCID
func (c *EARContext) UserID() string { return c.Oci.UserID }

// Fingerprint returns the API key fingerprint
func (c *EARContext) Fingerprint() string { return c.Oci.Fingerprint }

// PrivateKey reports whether a private key is stored, without printing the PEM
func (c *EARContext) PrivateKey() string {
	if util.IsEmptyString(c.Oci.PrivateKeyContent) {
		return ""
	}
	return "Set"
}

// KeyName returns the key display name
func (c *EARContext) KeyName() string { return c.Oci.KeyName }

// KeyOCID returns the resolved key OCID
func (c *EARContext) KeyOCID() string { return c.Oci.KeyOCID }

// MarshalJSON function
func (c *EARContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Oci)
}

// ProviderContext for provider outputs
type ProviderContext struct {
	formatter.HeaderContext
	formatter.Context
	Oci ybaclient.OCICloudInfo
}

// RegionContext for provider outputs
type RegionContext struct {
	formatter.HeaderContext
	formatter.Context
	Region ybaclient.OCIRegionCloudInfo
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

// NewProviderContext creates a new context for rendering provider
func NewProviderContext() *ProviderContext {
	ociProviderCtx := ProviderContext{}
	ociProviderCtx.Header = formatter.SubHeaderContext{
		"AuthType":      providerAuthTypeHeader,
		"TenancyID":     providerTenancyIDHeader,
		"UserID":        providerUserIDHeader,
		"Fingerprint":   providerFingerprintHeader,
		"CompartmentID": providerCompartmentIDHeader,
		"Region":        providerRegionHeader,
		"HostedZoneID":  providerHostedZoneIDHeader,
		"VpcType":       providerVpcTypeHeader,
	}
	return &ociProviderCtx
}

// NewRegionContext creates a new context for rendering provider
func NewRegionContext() *RegionContext {
	ociRegionCtx := RegionContext{}
	ociRegionCtx.Header = formatter.SubHeaderContext{
		"VNet":             providerVnetHeader,
		"InstanceTemplate": providerInstanceTemplateHeader,
	}
	return &ociRegionCtx
}

// AuthType fetches OCI authentication type
func (c *ProviderContext) AuthType() string {
	return c.Oci.GetOciAuthType()
}

// TenancyID fetches OCI tenancy OCID
func (c *ProviderContext) TenancyID() string {
	return c.Oci.GetOciTenancyId()
}

// UserID fetches OCI user OCID
func (c *ProviderContext) UserID() string {
	return c.Oci.GetOciUserId()
}

// Fingerprint fetches OCI API key fingerprint
func (c *ProviderContext) Fingerprint() string {
	return c.Oci.GetOciFingerprint()
}

// CompartmentID fetches OCI compartment OCID
func (c *ProviderContext) CompartmentID() string {
	return c.Oci.GetOciCompartmentId()
}

// Region fetches the default OCI region
func (c *ProviderContext) Region() string {
	return c.Oci.GetOciRegion()
}

// HostedZoneID fetches OCI hosted zone OCID
func (c *ProviderContext) HostedZoneID() string {
	return c.Oci.GetOciHostedZoneId()
}

// VpcType fetches VPC Type
func (c *ProviderContext) VpcType() string {
	return c.Oci.GetVpcType()
}

// MarshalJSON function
func (c *ProviderContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Oci)
}

// VNet fetches OCI Region virtual network
func (c *RegionContext) VNet() string {
	return c.Region.GetVnet()
}

// InstanceTemplate fetches OCI Region instance configuration OCID
func (c *RegionContext) InstanceTemplate() string {
	return c.Region.GetInstanceTemplate()
}

// MarshalJSON function
func (c *RegionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Region)
}
