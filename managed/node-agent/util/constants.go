// Copyright (c) YugaByte, Inc.
package util

import (
	"fmt"
	"os"
	"sync"
)

var (
	homeDirectory   *string
	onceLoadHomeDir = &sync.Once{}
)

const (
	DefaultConfig           = "config"
	preflightCheckScript    = "/pkg/scripts/preflight_check.sh"
	nodeAgentDir            = "/node-agent"
	configDir               = "/config"
	certsDir                = "/cert"
	releaseDir              = "/release"
	logsDir                 = "/logs"
	DefaultShell            = "/bin/bash"
	PlatformApiTokenHeader  = "X-AUTH-YW-API-TOKEN"
	PlatformJwtTokenHeader  = "X-AUTH-YW-API-JWT"
	JwtUserIdClaim          = "userId"
	JwtClientIdClaim        = "clientId"
	JwtClientTypeClaim      = "clientType"
	JwtIssuer               = "https://www.yugabyte.com"
	JwtSubject              = "NODE_AGENT"
	JwtExpirationTime       = 600 //in seconds
	NodeAgentDefaultLog     = "node_agent.log"
	NodeHomeDirectory       = "/home/yugabyte"
	GetCustomersApiEndpoint = "/api/customers"
	GetVersionEndpoint      = "/api/app_version"
	UpgradeScript           = "yb-node-agent.sh"
	InstallScript           = "node-agent-installer.sh"
)

// Returns the platform endpoint for fetching Providers.
func PlatformGetProvidersEndpoint(cuuid string) string {
	return fmt.Sprintf("/api/customers/%s/providers", cuuid)
}

// Returns the platform endpoint for fetching Users.
func PlatformGetUsersEndpoint(cuuid string) string {
	return fmt.Sprintf("/api/customers/%s/users", cuuid)
}

// Returns the platform endpoint for fetching instance types.
func PlatformGetInstanceTypesEndpoint(cuuid string, puuid string) string {
	return fmt.Sprintf("/api/customers/%s/providers/%s/instance_types", cuuid, puuid)
}

// Returns the platform endpoint for registering a node agent.
func PlatformRegisterAgentEndpoint(cuuid string) string {
	return fmt.Sprintf("/api/v1/customers/%s/node_agents", cuuid)
}

// Returns the platform endpoint for unregistering a node agent.
func PlatformUnregisterAgentEndpoint(cuuid string, nuuid string) string {
	return fmt.Sprintf("/api/v1/customers/%s/node_agents/%s", cuuid, nuuid)
}

// Returns the platform endpoint for getting the node agent state.
func PlatformGetAgentStateEndpoint(cuuid string, nuuid string) string {
	return fmt.Sprintf("/api/customers/%s/node_agents/%s/state", cuuid, nuuid)
}

// Returns the platform endpoint for updating the node agent state.
func PlatformPutAgentStateEndpoint(cuuid string, nuuid string) string {
	return fmt.Sprintf("/api/customers/%s/node_agents/%s/state", cuuid, nuuid)
}

// Returns the platform endpoint for updating the node agent state.
func PlatformPutAgentEndpoint(cuuid string, nuuid string) string {
	return fmt.Sprintf("/api/customers/%s/node_agents/%s", cuuid, nuuid)
}

// Returns the platform endpoint for fetching instance_type details.
func PlatformGetConfigEndpoint(cuuid string, puuid string, instance_type string) string {
	return fmt.Sprintf(
		"/api/customers/%s/providers/%s/instance_types/%s",
		cuuid,
		puuid,
		instance_type,
	)
}

// Returns the platform endpoint for posting the node capabilites
// and adding node instane to the platform.
func PlatformPostNodeCapabilitiesEndpoint(cuuid string, azid string) string {
	return fmt.Sprintf("/api/customers/%s/zones/%s/nodes", cuuid, azid)
}

// Returns the home directory.
func MustGetHomeDirectory() string {
	if homeDirectory == nil {
		onceLoadHomeDir.Do(func() {
			homeDirName, err := os.UserHomeDir()
			if err != nil {
				panic("Unable to fetch the Home Directory")
			} else {
				homeDirectory = &homeDirName
			}
		})
	}
	return *homeDirectory + nodeAgentDir
}

// Returns the Path to Preflight Checks script
// which should be present in  ~/scripts folder.
func GetPreflightCheckPath() string {
	return MustGetHomeDirectory() + preflightCheckScript
}

// Returns the config directory path.
// All the config files should
// be present in this directory.
func GetConfigDir() string {
	return MustGetHomeDirectory() + configDir
}

// Returns path to the Certs directory.
func GetCertsDir() string {
	return MustGetHomeDirectory() + certsDir
}

func GetReleaseDir() string {
	return MustGetHomeDirectory() + releaseDir
}

// Returns path to the Logs directory.
func GetLogsDir() string {
	return MustGetHomeDirectory() + logsDir
}

// Returns path to the installer/upgrade script.
func GetUpgradeScriptPath() string {
	return MustGetHomeDirectory() + "/pkg/bin/" + UpgradeScript
}

func GetInstallScriptPath() string {
	return MustGetHomeDirectory() + "/" + InstallScript
}
