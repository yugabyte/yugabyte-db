/*
 * Copyright (c) YugaByte, Inc.
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

// CreateTime fetches the alert Create Time
func (c *Context) CreateTime() string {
	return util.PrintTime(c.tp.GetCreateTime())
}

// UpdateTime fetches whether Backup UpdateTime
func (c *Context) UpdateTime() string {
	return util.PrintTime(c.tp.GetUpdateTime())
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

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.tp)
}
