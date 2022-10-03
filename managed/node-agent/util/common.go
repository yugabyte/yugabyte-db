package util

import (
	"context"
	"errors"
	"fmt"
	"net/url"
	"os"
	"sync"

	"github.com/google/uuid"
)

const (
	// Node agent common constants.
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
	// Cert names.
	NodeAgentCertFile = "node_agent.crt"
	NodeAgentKeyFile  = "node_agent.key"

	NodePort = "9070"

	// Platform config keys.
	PlatformUrlKey           = "platform.url"
	CustomerIdKey            = "platform.cuuid"
	UserIdKey                = "platform.userId"
	ProviderIdKey            = "platform.puuid"
	PlatformCertsKey         = "platform.certs"
	PlatformCertsUpgradeKey  = "platform.upgrade_certs"
	PlatformVersionKey       = "platform.version"
	PlatformVersionUpdateKey = "platform.update_version"

	// Node config keys.
	NodeIpKey           = "node.ip"
	NodePortKey         = "node.port"
	RequestTimeoutKey   = "node.request_timeout_sec"
	NodeNameKey         = "node.name"
	NodeAgentIdKey      = "node.agent.uuid"
	NodeIdKey           = "node.uuid"
	NodeInstanceTypeKey = "node.instance_type"
	NodeAzIdKey         = "node.azid"
	NodeRegionKey       = "node.region"
	NodeZoneKey         = "node.zone"
	NodeInstanceNameKey = "node.instance_name"
	NodePingIntervalKey = "node.ping_interval_sec"
	NodeLoggerKey       = "node.log"
)

var (
	homeDirectory   *string
	onceLoadHomeDir = &sync.Once{}
)

type Handler func(context.Context) (any, error)

func NewUUID() uuid.UUID {
	return uuid.New()
}

func ExtractBaseURL(value string) (string, error) {
	parsedUrl, err := url.Parse(value)
	if err != nil {
		return "", errors.New("Malformed platform URL")
	}
	var baseUrl string
	if parsedUrl.Port() == "" {
		baseUrl = fmt.Sprintf("%s://%s", parsedUrl.Scheme, parsedUrl.Hostname())
	}
	baseUrl = fmt.Sprintf(
		"%s://%s:%s",
		parsedUrl.Scheme,
		parsedUrl.Hostname(),
		parsedUrl.Port(),
	)
	return baseUrl, nil
}

// Returns the platform endpoint for fetching providers.
func PlatformGetProvidersEndpoint(cuuid string) string {
	return fmt.Sprintf("/api/customers/%s/providers", cuuid)
}

// Returns the platform endpoint for fetching the provider.
func PlatformGetProviderEndpoint(cuuid, puuid string) string {
	return fmt.Sprintf("/api/customers/%s/providers/%s", cuuid, puuid)
}

// Returns the platform endpoint for fetching access keys for a provider.
func PlatformGetAccessKeysEndpoint(cuuid, puuid string) string {
	return fmt.Sprintf("/api/customers/%s/providers/%s/access_keys", cuuid, puuid)
}

// Returns the platform endpoint for fetching Users.
func PlatformGetUsersEndpoint(cuuid string) string {
	return fmt.Sprintf("/api/customers/%s/users", cuuid)
}

// Returns the platform endpoint for getting the user.
func PlatformGetUserEndpoint(cuuid, uuid string) string {
	return fmt.Sprintf("/api/customers/%s/users/%s", cuuid, uuid)
}

// Returns the platform endpoint for fetching SessionInfo.
func PlatformGetSessionInfoEndpoint() string {
	return "/api/session_info"
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
func PlatformGetInstanceTypeEndpoint(cuuid string, puuid string, instance_type string) string {
	return fmt.Sprintf(
		"/api/customers/%s/providers/%s/instance_types/%s",
		cuuid,
		puuid,
		instance_type,
	)
}

// Returns the platform endpoint for posting the node instances.
// and adding node instane to the platform.
func PlatformPostNodeInstancesEndpoint(cuuid string, azid string) string {
	return fmt.Sprintf("/api/customers/%s/zones/%s/nodes", cuuid, azid)
}

// Returns the platform endpoint for validating the node configs.
func PlatformValidateNodeInstanceEndpoint(cuuid string, azid string) string {
	return fmt.Sprintf("/api/customers/%s/zones/%s/nodes/validate", cuuid, azid)
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
func PreflightCheckPath() string {
	return MustGetHomeDirectory() + preflightCheckScript
}

// Returns the config directory path.
// All the config files should
// be present in this directory.
func ConfigDir() string {
	return MustGetHomeDirectory() + configDir
}

// Returns path to the Certs directory.
func CertsDir() string {
	return MustGetHomeDirectory() + certsDir
}

func ReleaseDir() string {
	return MustGetHomeDirectory() + releaseDir
}

// Returns path to the Logs directory.
func LogsDir() string {
	return MustGetHomeDirectory() + logsDir
}

// Returns path to the installer/upgrade script.
func UpgradeScriptPath() string {
	return MustGetHomeDirectory() + "/pkg/bin/" + UpgradeScript
}

func InstallScriptPath() string {
	return MustGetHomeDirectory() + "/" + InstallScript
}

func VersionFile() string {
	return MustGetHomeDirectory() + "/pkg/version_metadata.json"
}

func IsDigits(str string) bool {
	if str == "" {
		return false
	}
	runes := []rune(str)
	for _, r := range runes {
		if r < '0' || r > '9' {
			return false
		}
	}
	return true
}
