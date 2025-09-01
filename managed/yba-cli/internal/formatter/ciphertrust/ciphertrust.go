/*
 * Copyright (c) YugaByte, Inc.
 */

package ciphertrust

import (
	"encoding/json"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// EAR1 for EAR listing
	EAR1 = "table {{.ManagerURL}}\t{{.AuthType}}"

	// EAR2 for EAR listing
	EAR2 = "table {{.RefreshToken}}"

	// EAR3 for EAR listing
	EAR3 = "table {{.Username}}\t{{.Password}}"

	// EAR4 for EAR listing
	EAR4 = "table {{.KeyName}}\t{{.KeyAlgorithm}}\t{{.KeySize}}"

	managerURLHeader   = "CipherTrust Manager URL"
	authTypeHeader     = "Auth Type"
	refreshTokenHeader = "Refresh Token"
	usernameHeader     = "Username"
	passwordHeader     = "Password"
	keyNameHeader      = "Key Name"
	keyAlgorithmHeader = "Key Algorithm"
	keySizeHeader      = "Key Size (bits)"
)

// EARContext for ksm outputs
type EARContext struct {
	formatter.HeaderContext
	formatter.Context
	CT util.CipherTrustConfigField
}

// NewEARContext creates a new context for rendering kms config
func NewEARContext() *EARContext {
	ctEARCtx := EARContext{}
	ctEARCtx.Header = formatter.SubHeaderContext{
		"ManagerURL":   managerURLHeader,
		"AuthType":     authTypeHeader,
		"RefreshToken": refreshTokenHeader,
		"Username":     usernameHeader,
		"Password":     passwordHeader,
		"KeyName":      keyNameHeader,
		"KeyAlgorithm": keyAlgorithmHeader,
		"KeySize":      keySizeHeader,
	}
	return &ctEARCtx
}

// NewEARFormat returns a new EAR format
func NewEARFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(EAR1)
	default:
		return formatter.Format(source)
	}
}

// ManagerURL returns CipherTrust Manager URL
func (c *EARContext) ManagerURL() string { return c.CT.ManagerURL }

// AuthType returns auth type
func (c *EARContext) AuthType() string { return c.CT.AuthType }

// RefreshToken returns refresh token
func (c *EARContext) RefreshToken() string { return c.CT.RefreshToken }

// Username returns username
func (c *EARContext) Username() string { return c.CT.Username }

// Password returns password
func (c *EARContext) Password() string { return c.CT.Password }

// KeyName returns key name
func (c *EARContext) KeyName() string { return c.CT.KeyName }

// KeyAlgorithm returns key algorithm
func (c *EARContext) KeyAlgorithm() string { return c.CT.KeyAlgorithm }

// KeySize returns key size
func (c *EARContext) KeySize() float64 { return c.CT.KeySize }

// MarshalJSON function
func (c *EARContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.CT)
}
