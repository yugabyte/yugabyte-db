/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryprovider

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultTelemetryProviderListing = "table {{.Name}}\t{{.Type}}\t{{.UUID}}"
	telemetryProvider1              = "table {{.CreateTime}}\t{{.UpdateTime}}"
	telemetryProvider2              = "table {{.Tags}}"
	tagsHeader                      = "Tags"

	gcpType       = "table {{.Project}}"
	projectHeader = "Project"

	dataDogType  = "table {{.Site}}\t{{.ApiKey}}"
	siteHeader   = "Site"
	apiKeyHeader = "API Key"

	splunkType1      = "table {{.Endpoint}}\t{{.Token}}\t{{.Index}}"
	splunkType2      = "table {{.SourceType}}\t{{.Source}}"
	endpointHeader   = "Endpoint"
	tokenHeader      = "Token"
	indexHeader      = "Index"
	sourceTypeHeader = "Source Type"
	sourceHeader     = "Source"

	awsType3 = "table {{.Endpoint}}\t{{.AccessKey}}\t{{.SecretKey}}"
	awsType2 = "table {{.Region}}\t{{.RoleARN}}"
	awsType1 = "table {{.LogGroup}}\t{{.LogStream}}"

	accessKeyHeader = "Access Key"
	secretKeyHeader = "Secret Key"
	regionHeader    = "Region"
	roleARNHeader   = "Role ARN"
	logGroupHeader  = "Log Group"
	logStreamHeader = "Log Stream"

	lokiType1            = "table {{.Endpoint}}\t{{.OrganizationID}}\t{{.AuthType}}"
	lokiType2            = "table {{.Username}}\t{{.Password}}"
	organizationIDHeader = "Organization ID"
	authTypeHeader       = "Auth Type"
	usernameHeader       = "Username"
	passwordHeader       = "Password"

	dynatraceType  = "table {{.Endpoint}}\t{{.ApiToken}}"
	apiTokenHeader = "API Token"

	s3Type1 = "table {{.Bucket}}\t{{.Region}}\t{{.Partition}}"
	s3Type2 = "table {{.AccessKey}}\t{{.SecretKey}}\t{{.RoleArn}}"
	s3Type3 = "table {{.DirectoryPrefix}}\t{{.FilePrefix}}\t{{.Marshaler}}"
	s3Type4 = "table {{.Endpoint}}\t{{.DisableSSL}}\t{{.ForcePathStyle}}"
	s3Type5 = "table {{.IncludeUniverseAndNodeInPrefix}}"

	bucketHeader                         = "Bucket"
	partitionHeader                      = "Partition"
	roleArnHeader                        = "Role ARN"
	directoryPrefixHeader                = "Directory Prefix"
	filePrefixHeader                     = "File Prefix"
	marshalerHeader                      = "Marshaler"
	disableSSLHeader                     = "Disable SSL"
	forcePathStyleHeader                 = "Force Path Style"
	includeUniverseAndNodeInPrefixHeader = "Include Universe And Node In Prefix"

	otlpType1  = "table {{.Endpoint}}\t{{.Protocol}}\t{{.AuthType}}"
	otlpType2  = "table {{.Compression}}\t{{.TimeoutSeconds}}"
	otlpType3  = "table {{.LogsEndpoint}}\t{{.MetricsEndpoint}}"
	otlpType4  = "table {{.Headers}}"
	otlpBearer = "table {{.BearerToken}}"
	otlpRetry  = "table {{.RetryEnabled}}\t{{.RetryInitialInterval}}\t" +
		"{{.RetryMaxInterval}}\t{{.RetryMaxElapsedTime}}"

	protocolHeader             = "Protocol"
	compressionHeader          = "Compression"
	timeoutSecondsHeader       = "Timeout (seconds)"
	logsEndpointHeader         = "Logs Endpoint"
	metricsEndpointHeader      = "Metrics Endpoint"
	headersHeader              = "Headers"
	bearerTokenHeader          = "Bearer Token"
	retryEnabledHeader         = "Retry Enabled"
	retryInitialIntervalHeader = "Retry Initial Interval"
	retryMaxIntervalHeader     = "Retry Max Interval"
	retryMaxElapsedTimeHeader  = "Retry Max Elapsed Time"
)

// Context for telemetry provider outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	tp util.TelemetryProvider
}

// NewTelemetryProviderFormat for formatting output
func NewTelemetryProviderFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTelemetryProviderListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of TelemetryProviders
func Write(ctx formatter.Context, telemetryProviders []util.TelemetryProvider) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of telemetry providers into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(telemetryProviders, "", "  ")
		} else {
			output, err = json.Marshal(telemetryProviders)
		}

		if err != nil {
			logrus.Errorf("Error marshaling telemetry providers to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, telemetryProvider := range telemetryProviders {
			err := format(&Context{tp: telemetryProvider})
			if err != nil {
				logrus.Debugf("Error rendering telemetry provider: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewTelemetryProviderContext(), render)
}

// NewTelemetryProviderContext creates a new context for rendering telemetry provider
func NewTelemetryProviderContext() *Context {
	telemetryProviderCtx := Context{}
	telemetryProviderCtx.Header = formatter.SubHeaderContext{
		"Name":       formatter.NameHeader,
		"UUID":       formatter.UUIDHeader,
		"Type":       formatter.TypeHeader,
		"CreateTime": formatter.CreateTimeHeader,
		"UpdateTime": formatter.UpdateTimeHeader,
		"Tags":       tagsHeader,

		"Project": projectHeader,

		"Site":   siteHeader,
		"ApiKey": apiKeyHeader,

		"Endpoint":   endpointHeader,
		"Token":      tokenHeader,
		"Index":      indexHeader,
		"SourceType": sourceTypeHeader,
		"Source":     sourceHeader,

		"AccessKey": accessKeyHeader,
		"SecretKey": secretKeyHeader,
		"Region":    regionHeader,
		"RoleARN":   roleARNHeader,
		"LogGroup":  logGroupHeader,
		"LogStream": logStreamHeader,

		"OrganizationID": organizationIDHeader,
		"AuthType":       authTypeHeader,
		"Username":       usernameHeader,
		"Password":       passwordHeader,

		"ApiToken": apiTokenHeader,

		"Bucket":                         bucketHeader,
		"Partition":                      partitionHeader,
		"RoleArn":                        roleArnHeader,
		"DirectoryPrefix":                directoryPrefixHeader,
		"FilePrefix":                     filePrefixHeader,
		"Marshaler":                      marshalerHeader,
		"DisableSSL":                     disableSSLHeader,
		"ForcePathStyle":                 forcePathStyleHeader,
		"IncludeUniverseAndNodeInPrefix": includeUniverseAndNodeInPrefixHeader,

		"Protocol":             protocolHeader,
		"Compression":          compressionHeader,
		"TimeoutSeconds":       timeoutSecondsHeader,
		"LogsEndpoint":         logsEndpointHeader,
		"MetricsEndpoint":      metricsEndpointHeader,
		"Headers":              headersHeader,
		"BearerToken":          bearerTokenHeader,
		"RetryEnabled":         retryEnabledHeader,
		"RetryInitialInterval": retryInitialIntervalHeader,
		"RetryMaxInterval":     retryMaxIntervalHeader,
		"RetryMaxElapsedTime":  retryMaxElapsedTimeHeader,
	}
	return &telemetryProviderCtx
}

// UUID returns the UUID of the telemetry provider
func (c *Context) UUID() string {
	return c.tp.GetUuid()
}

// Name returns the name of the telemetry provider
func (c *Context) Name() string {
	return c.tp.GetName()
}

// Type returns the code of the telemetry provider
func (c *Context) Type() string {
	config := c.tp.GetConfig()
	return config.GetType()
}

// CreateTime fetches the create time. Providers created before the entity gained
// @WhenCreated have none, so render "-" rather than an empty cell.
func (c *Context) CreateTime() string {
	createTime := util.PrintTime(c.tp.GetCreateTime())
	if util.IsEmptyString(createTime) {
		return "-"
	}
	return createTime
}

// UpdateTime fetches the telemetry provider update time
func (c *Context) UpdateTime() string {
	updateTime := util.PrintTime(c.tp.GetUpdateTime())
	if util.IsEmptyString(updateTime) {
		return "-"
	}
	return updateTime
}

// Tags fetches map as string
func (c *Context) Tags() string {
	tags := ""
	tagsMap := c.tp.GetTags()
	for k, v := range tagsMap {
		tags = fmt.Sprintf("%s%s : %s\n", tags, k, v)
	}
	if len(tags) == 0 {
		return "-"
	}
	tags = tags[0 : len(tags)-1]
	return tags
}

// Project fetches the project of the telemetry provider
func (c *Context) Project() string {
	config := c.tp.GetConfig()
	return config.GetProject()
}

// Site fetches the site of the telemetry provider
func (c *Context) Site() string {
	config := c.tp.GetConfig()
	return config.GetSite()
}

// ApiKey fetches the API key of the telemetry provider
func (c *Context) ApiKey() string {
	config := c.tp.GetConfig()
	return config.GetApiKey()
}

// Endpoint fetches the endpoint of the telemetry provider
func (c *Context) Endpoint() string {
	config := c.tp.GetConfig()
	return config.GetEndpoint()
}

// Token fetches the token of the telemetry provider
func (c *Context) Token() string {
	config := c.tp.GetConfig()
	return config.GetToken()
}

// Index fetches the index of the telemetry provider
func (c *Context) Index() string {
	config := c.tp.GetConfig()
	return config.GetIndex()
}

// SourceType fetches the source type of the telemetry provider
func (c *Context) SourceType() string {
	config := c.tp.GetConfig()
	return config.GetSourceType()
}

// Source fetches the source of the telemetry provider
func (c *Context) Source() string {
	config := c.tp.GetConfig()
	return config.GetSource()
}

// AccessKey fetches the access key of the telemetry provider
func (c *Context) AccessKey() string {
	config := c.tp.GetConfig()
	return config.GetAccessKey()
}

// SecretKey fetches the secret key of the telemetry provider
func (c *Context) SecretKey() string {
	config := c.tp.GetConfig()
	return config.GetSecretKey()
}

// Region fetches the region of the telemetry provider
func (c *Context) Region() string {
	config := c.tp.GetConfig()
	return config.GetRegion()
}

// RoleARN fetches the role ARN of the telemetry provider
func (c *Context) RoleARN() string {
	config := c.tp.GetConfig()
	return config.GetRoleARN()
}

// LogGroup fetches the log group of the telemetry provider
func (c *Context) LogGroup() string {
	config := c.tp.GetConfig()
	return config.GetLogGroup()
}

// LogStream fetches the log stream of the telemetry provider
func (c *Context) LogStream() string {
	config := c.tp.GetConfig()
	return config.GetLogStream()
}

// OrganizationID fetches the organization ID of the telemetry provider
func (c *Context) OrganizationID() string {
	config := c.tp.GetConfig()
	return config.GetOrganizationID()
}

// AuthType fetches the auth type of the telemetry provider
func (c *Context) AuthType() string {
	config := c.tp.GetConfig()
	return config.GetAuthType()
}

// Username fetches the username of the telemetry provider
func (c *Context) Username() string {
	config := c.tp.GetConfig()
	basicAuth := config.GetBasicAuth()
	if basicAuth.GetUsername() == "" {
		return ""
	}
	return basicAuth.GetUsername()
}

// Password fetches the password of the telemetry provider
func (c *Context) Password() string {
	config := c.tp.GetConfig()
	basicAuth := config.GetBasicAuth()
	if basicAuth.GetPassword() == "" {
		return ""
	}
	return basicAuth.GetPassword()
}

// ApiToken fetches the Dynatrace API token
func (c *Context) ApiToken() string {
	config := c.tp.GetConfig()
	return config.GetApiToken()
}

// Bucket fetches the S3 bucket
func (c *Context) Bucket() string {
	config := c.tp.GetConfig()
	return config.GetBucket()
}

// Partition fetches the S3 partition granularity
func (c *Context) Partition() string {
	config := c.tp.GetConfig()
	return config.GetPartition()
}

// RoleArn fetches the S3 role ARN
func (c *Context) RoleArn() string {
	config := c.tp.GetConfig()
	return config.GetRoleArn()
}

// DirectoryPrefix fetches the S3 directory prefix
func (c *Context) DirectoryPrefix() string {
	config := c.tp.GetConfig()
	return config.GetDirectoryPrefix()
}

// FilePrefix fetches the S3 file prefix
func (c *Context) FilePrefix() string {
	config := c.tp.GetConfig()
	return config.GetFilePrefix()
}

// Marshaler fetches the S3 marshaler
func (c *Context) Marshaler() string {
	config := c.tp.GetConfig()
	return config.GetMarshaler()
}

// DisableSSL fetches whether SSL is disabled
func (c *Context) DisableSSL() string {
	config := c.tp.GetConfig()
	return fmt.Sprintf("%t", config.GetDisableSSL())
}

// ForcePathStyle fetches whether path style addressing is forced
func (c *Context) ForcePathStyle() string {
	config := c.tp.GetConfig()
	return fmt.Sprintf("%t", config.GetForcePathStyle())
}

// IncludeUniverseAndNodeInPrefix fetches whether the universe UUID and node name are
// appended to the S3 prefix
func (c *Context) IncludeUniverseAndNodeInPrefix() string {
	config := c.tp.GetConfig()
	return fmt.Sprintf("%t", config.GetIncludeUniverseAndNodeInPrefix())
}

// Protocol fetches the OTLP protocol
func (c *Context) Protocol() string {
	config := c.tp.GetConfig()
	return config.GetProtocol()
}

// Compression fetches the OTLP compression
func (c *Context) Compression() string {
	config := c.tp.GetConfig()
	return config.GetCompression()
}

// TimeoutSeconds fetches the OTLP export timeout
func (c *Context) TimeoutSeconds() string {
	config := c.tp.GetConfig()
	return fmt.Sprintf("%d", config.GetTimeoutSeconds())
}

// LogsEndpoint fetches the OTLP logs endpoint override
func (c *Context) LogsEndpoint() string {
	config := c.tp.GetConfig()
	return config.GetLogsEndpoint()
}

// MetricsEndpoint fetches the OTLP metrics endpoint override
func (c *Context) MetricsEndpoint() string {
	config := c.tp.GetConfig()
	return config.GetMetricsEndpoint()
}

// Headers fetches the OTLP headers as a printable string
func (c *Context) Headers() string {
	config := c.tp.GetConfig()
	return MapToString(config.GetHeaders())
}

// BearerToken fetches the OTLP bearer token
func (c *Context) BearerToken() string {
	config := c.tp.GetConfig()
	bearerToken := config.GetBearerToken()
	return bearerToken.GetToken()
}

// RetryEnabled fetches whether exporter retry is enabled
func (c *Context) RetryEnabled() string {
	config := c.tp.GetConfig()
	retry := config.GetRetryOnFailure()
	return fmt.Sprintf("%t", retry.GetEnabled())
}

// RetryInitialInterval fetches the exporter retry initial interval
func (c *Context) RetryInitialInterval() string {
	config := c.tp.GetConfig()
	retry := config.GetRetryOnFailure()
	return retry.GetInitialInterval()
}

// RetryMaxInterval fetches the exporter retry maximum interval
func (c *Context) RetryMaxInterval() string {
	config := c.tp.GetConfig()
	retry := config.GetRetryOnFailure()
	return retry.GetMaxInterval()
}

// RetryMaxElapsedTime fetches the exporter retry maximum elapsed time
func (c *Context) RetryMaxElapsedTime() string {
	config := c.tp.GetConfig()
	retry := config.GetRetryOnFailure()
	return retry.GetMaxElapsedTime()
}

// MapToString renders a string map as newline separated "key : value" pairs, matching how
// tags are rendered. Returns "-" for an empty map so table cells are never blank.
func MapToString(m map[string]string) string {
	out := ""
	for k, v := range m {
		out = fmt.Sprintf("%s%s : %s\n", out, k, v)
	}
	if len(out) == 0 {
		return "-"
	}
	return out[0 : len(out)-1]
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.tp)
}
