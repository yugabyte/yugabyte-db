/*
 * Copyright (c) YugaByte, Inc.
 */

package util

// OIDC keys and flags for YBA
const (
	ToggleOIDCKey               = "yb.security.use_oauth"
	SecurityTypeKey             = "yb.security.type"
	OIDCClientIDKey             = "yb.security.clientID"
	OIDCClientSecretKey         = "yb.security.secret"
	OIDCDiscoveryURLKey         = "yb.security.discoveryURI"
	OIDCScopeKey                = "yb.security.oidcScope"
	OIDCEmailAttributeKey       = "yb.security.oidcEmailAttribute"
	OIDCRefreshTokenEndpointKey = "yb.security.oidcRefreshTokenEndpoint"
	OIDCProviderMetadataKey     = "yb.security.oidcProviderMetadata"
	OIDCAutoCreateUserKey       = "yb.security.oidc_enable_auto_create_users"
	OIDCDefaultRoleKey          = "yb.security.oidc_default_role"
	OIDCGroupClaimKey           = "yb.security.oidc_group_claim"
)

// OidcKeyToFlagMap is a map of OIDC keys to their corresponding flags
var OidcKeyToFlagMap = map[string]string{
	ToggleOIDCKey:               "oidc-enabled",
	SecurityTypeKey:             "security-type",
	OIDCClientIDKey:             "client-id",
	OIDCClientSecretKey:         "client-secret",
	OIDCDiscoveryURLKey:         "discovery-url",
	OIDCScopeKey:                "scope",
	OIDCEmailAttributeKey:       "email-attribute",
	OIDCRefreshTokenEndpointKey: "refresh-token-url",
	OIDCProviderMetadataKey:     "provider-configuration",
	OIDCAutoCreateUserKey:       "auto-create-user",
	OIDCDefaultRoleKey:          "default-role",
	OIDCGroupClaimKey:           "group-claim",
}

// IsOIDCKey checks if the given key is an OIDC key
func IsOIDCKey(key string) bool {
	_, exists := OidcKeyToFlagMap[key]
	return exists
}

// ResetFlagToOidcKeyMap is a map of OIDC keys to their corresponding reset flags
var ResetFlagToOidcKeyMap = map[string]string{
	"client-id":              OIDCClientIDKey,
	"client-secret":          OIDCClientSecretKey,
	"discovery-url":          OIDCDiscoveryURLKey,
	"scope":                  OIDCScopeKey,
	"email-attribute":        OIDCEmailAttributeKey,
	"refresh-token-url":      OIDCRefreshTokenEndpointKey,
	"provider-configuration": OIDCProviderMetadataKey,
	"auto-create-user":       OIDCAutoCreateUserKey,
	"default-role":           OIDCDefaultRoleKey,
	"group-claim":            OIDCGroupClaimKey,
}

// GetOIDCKeyForResetFlag returns the OIDC key for the given reset flag
func GetOIDCKeyForResetFlag(flag string) (string, bool) {
	key, exists := ResetFlagToOidcKeyMap[flag]
	return key, exists
}
