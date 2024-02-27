package util

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"net/url"
	pb "node-agent/generated/service"
	"os"
	"os/user"
	"path"
	"reflect"
	"strconv"
	"sync"

	"github.com/google/uuid"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

const (
	// Node agent common constants.
	DefaultConfig           = "config"
	preflightCheckScript    = "/pkg/scripts/preflight_check.sh"
	nodeAgentDir            = "/node-agent"
	configDir               = "/config"
	certsDir                = "/cert"
	pexEnvDir               = "/pkg/devops/pex/pexEnv"
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
	NodeAgentRegistryPath   = ".yugabyte/node-agent-registry"
	GetCustomersApiEndpoint = "/api/customers"
	GetVersionEndpoint      = "/api/app_version"
	UpgradeScript           = "node-agent-installer.sh"
	RequestIdHeader         = "X-REQUEST-ID"

	// Cert names.
	NodeAgentCertFile = "node_agent.crt"
	NodeAgentKeyFile  = "node_agent.key"

	NodePort = "9070"

	// Platform config keys.
	PlatformUrlKey            = "platform.url"
	CustomerIdKey             = "platform.cuuid"
	UserIdKey                 = "platform.userId"
	ProviderIdKey             = "platform.puuid"
	PlatformCertsKey          = "platform.certs"
	PlatformCertsUpgradeKey   = "platform.upgrade_certs"
	PlatformVersionKey        = "platform.version"
	PlatformVersionUpdateKey  = "platform.update_version"
	PlatformSkipVerifyCertKey = "platform.skip_verify_cert"
	PlatformCaCertPathKey     = "platform.ca_cert_path"

	// Node config keys.
	NodeIpKey                  = "node.ip"
	NodeBindIpKey              = "node.bind_ip"
	NodePortKey                = "node.port"
	RequestTimeoutKey          = "node.request_timeout_sec"
	NodeNameKey                = "node.name"
	NodeAgentIdKey             = "node.agent.uuid"
	NodeIdKey                  = "node.uuid"
	NodeInstanceTypeKey        = "node.instance_type"
	NodeAzIdKey                = "node.azid"
	NodeRegionKey              = "node.region"
	NodeZoneKey                = "node.zone"
	NodeLoggerKey              = "node.log"
	NodeAgentRestartKey        = "node.restart"
	NodeAgentLogLevelKey       = "node.log_level"
	NodeAgentLogMaxMbKey       = "node.log_max_mb"
	NodeAgentLogMaxBackupsKey  = "node.log_max_backups"
	NodeAgentLogMaxDaysKey     = "node.log_max_days"
	NodeAgentDisableMetricsTLS = "node.disable_metrics_tls"

	// Node agent registry keys.
	NodeAgentRegistryHomeKey = "node_agent_home"

	// JWT claims.
	JwtClaimsExpiryKey  = "exp"
	JwtClaimsSessionKey = "ses"
)

const (
	CorrelationId ContextKey = "correlation-id"
)

var (
	nodeAgentHome         string
	onceLoadNodeAgentHome = &sync.Once{}
	ErrNotExist           = errors.New("Entity does not exist")
)

// ContextKey is the key type go context values.
type ContextKey string

// Handler is a generic handler func.
type Handler func(context.Context) (any, error)

// RPCResponseConverter is the converter for response in async executor.
type RPCResponseConverter func(any) (*pb.DescribeTaskResponse, error)

// UserDetail is a placeholder for OS user.
type UserDetail struct {
	User      *user.User
	UserID    uint32
	GroupID   uint32
	IsCurrent bool
}

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

// Returns the platform endpoint for getting a node agent by IP.
func PlatformGetNodeAgentEndpoint(cuuid string, ip string) string {
	return fmt.Sprintf("/api/v1/customers/%s/node_agents?nodeIp=%s", cuuid, ip)
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

// Returns the platform endpoint for fetching instanceType details.
func PlatformGetInstanceTypeEndpoint(cuuid string, puuid string, instanceType string) string {
	return fmt.Sprintf(
		"/api/customers/%s/providers/%s/instance_types/%s",
		cuuid,
		puuid,
		instanceType,
	)
}

// Returns the platform endpoint for posting the node instances.
// and adding node instance to the platform.
func PlatformPostNodeInstancesEndpoint(cuuid string, azid string) string {
	return fmt.Sprintf("/api/customers/%s/zones/%s/nodes", cuuid, azid)
}

// Returns the platform endpoint for validating the node configs.
func PlatformValidateNodeInstanceEndpoint(cuuid string, azid string) string {
	return fmt.Sprintf("/api/customers/%s/zones/%s/nodes/validate", cuuid, azid)
}

// Returns the platform endpoint for deleting a node instance.
func PlatformDeleteNodeInstanceEndpoint(cuuid string, puuid string, ip string) string {
	return fmt.Sprintf("/api/customers/%s/providers/%s/instances/%s", cuuid, puuid, ip)
}

// Returns the home directory.
func MustGetHomeDirectory() string {
	onceLoadNodeAgentHome.Do(func() {
		userHome, err := os.UserHomeDir()
		if err != nil {
			panic(fmt.Sprintf("Unable to fetch the Home Directory - %s", err.Error()))
		}
		// Check the registry first.
		registryPath := path.Join(userHome, NodeAgentRegistryPath)
		if config, err := ConfigWithName(registryPath); err == nil {
			nodeAgentHome = config.String(NodeAgentRegistryHomeKey)
		}
		if nodeAgentHome == "" {
			nodeAgentHome = userHome + nodeAgentDir
		}
	})
	return nodeAgentHome
}

// InstallDir returns the installation directory.
func InstallDir() string {
	return path.Dir(path.Clean(MustGetHomeDirectory()))
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

// PexEnvDir returns the pexEnv path
func PexEnvDir() string {
	return MustGetHomeDirectory() + pexEnvDir
}

// ReleaseDir returns the release dir path.
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

// UserInfo returns the user, user ID and group ID for the user name.
func UserInfo(username string) (*UserDetail, error) {
	userAcc, err := user.Current()
	if err != nil {
		return nil, err
	}
	isCurrent := true
	if username != "" && userAcc.Username != username {
		userAcc, err = user.Lookup(username)
		if err != nil {
			return nil, err
		}
		isCurrent = false
	}
	uid, err := strconv.Atoi(userAcc.Uid)
	if err != nil {
		return nil, err
	}
	gid, err := strconv.Atoi(userAcc.Gid)
	if err != nil {
		return nil, err
	}
	return &UserDetail{
		User: userAcc, UserID: uint32(uid), GroupID: uint32(gid), IsCurrent: isCurrent}, nil
}

// CorrelationID returns the correlation ID from the context.
func CorrelationID(ctx context.Context) string {
	if v := ctx.Value(CorrelationId); v != nil {
		return v.(string)
	}
	return ""
}

// WithCorrelationID creates a child context with correlation ID.
func WithCorrelationID(ctx context.Context, corrId string) context.Context {
	return context.WithValue(ctx, CorrelationId, corrId)
}

// ConvertType converts a type from one to another.
func ConvertType(from any, to any) error {
	kind := reflect.TypeOf(to).Kind()
	if kind != reflect.Pointer {
		return fmt.Errorf("Target type (%v) is not a pointer", kind)
	}
	var b []byte
	var err error
	if msg, ok := from.(proto.Message); ok {
		b, err = protojson.Marshal(msg)
	} else {
		b, err = json.Marshal(from)
	}
	if err != nil {
		return err
	}
	return json.Unmarshal(b, to)
}

// ScanDir scans a directory and invokes the callback for every file/dir.
func ScanDir(dir string, callback func(os.FileInfo) (bool, error)) error {
	fInfos, err := ioutil.ReadDir(dir)
	if err != nil {
		return err
	}
	for _, fInfo := range fInfos {
		isContinue, err := callback(fInfo)
		if err != nil {
			return err
		}
		if !isContinue {
			break
		}
	}
	return nil
}

// IsPexEnvAvailable returns true if pexEnv directory exists or there is no error.
func IsPexEnvAvailable() bool {
	fInfo, err := os.Stat(PexEnvDir())
	if err != nil {
		return false
	}
	return fInfo.IsDir()
}

// Indexable refers to indexable type.
type Indexable interface {
	Index() int
	SetIndex(int)
}

// PriorityQueue implements containers/heap.Interface.
type PriorityQueue[T interface{ Indexable }] struct {
	entries    []T
	comparator func(T, T) bool
}

// NewPriorityQueue returns an instance of PriorityQueue.
func NewPriorityQueue[T interface{ Indexable }](comparator func(T, T) bool) *PriorityQueue[T] {
	return &PriorityQueue[T]{
		entries:    []T{},
		comparator: comparator,
	}
}

// Len implements the method in container/heap.
func (queue *PriorityQueue[T]) Len() int { return len(queue.entries) }

// Less implements the method in container/heap.
func (queue *PriorityQueue[T]) Less(i, j int) bool {
	return queue.comparator(queue.entries[i], queue.entries[j])
}

// Swap implements the method in container/heap.
func (queue *PriorityQueue[T]) Swap(i, j int) {
	queue.entries[i], queue.entries[j] = queue.entries[j], queue.entries[i]
	queue.entries[i].SetIndex(i)
	queue.entries[j].SetIndex(j)
}

// Push implements the method in container/heap.
func (queue *PriorityQueue[T]) Push(item any) {
	entry := item.(T)
	queue.entries = append(queue.entries, entry)
	entry.SetIndex(len(queue.entries) - 1)
}

// Pop implements the method in container/heap.
func (queue *PriorityQueue[T]) Pop() any {
	var zero T
	n := len(queue.entries)
	entry := queue.entries[n-1]
	queue.entries[n-1] = zero
	queue.entries = queue.entries[0 : n-1]
	return entry
}

// Peek returns the least/top entry without removing it.
func (queue *PriorityQueue[T]) Peek() T {
	var zero T
	if len(queue.entries) > 0 {
		return queue.entries[0]
	}
	return zero
}
