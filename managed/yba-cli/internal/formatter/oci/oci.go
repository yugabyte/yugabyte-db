/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"encoding/json"

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

	authTypeHeader      = "Auth Type"
	regionHeader        = "Region"
	vaultIDHeader       = "Vault OCID"
	compartmentIDHeader = "Compartment OCID"
	tenancyIDHeader     = "Tenancy OCID"
	userIDHeader        = "User OCID"
	fingerprintHeader   = "Fingerprint"
	privateKeyHeader    = "Private Key"
	keyNameHeader       = "Key Name"
	keyOCIDHeader       = "Key OCID"
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
		"AuthType":      authTypeHeader,
		"Region":        regionHeader,
		"VaultID":       vaultIDHeader,
		"CompartmentID": compartmentIDHeader,
		"TenancyID":     tenancyIDHeader,
		"UserID":        userIDHeader,
		"Fingerprint":   fingerprintHeader,
		"PrivateKey":    privateKeyHeader,
		"KeyName":       keyNameHeader,
		"KeyOCID":       keyOCIDHeader,
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
