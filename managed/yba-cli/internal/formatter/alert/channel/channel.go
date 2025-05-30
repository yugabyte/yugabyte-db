/*
 * Copyright (c) YugaByte, Inc.
 */

package channel

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultAlertChannelListing = "table {{.Name}}\t{{.UUID}}\t{{.ChannelType}}"

	slackChannel = "table {{.Username}}\t{{.WebhookUrl}}\t{{.IconUrl}}"

	emailChannel1 = "table {{.SmtpData}}"

	emailChannel2 = "table {{.Recipients}}"

	webhookChannel = "table {{.WebhookUrl}}\t{{.SendResolved}}\t{{.HttpAuthType}}"

	basicWebhookChannel = "table {{.WebhookUsername}}\t{{.Password}}"

	tokenWebhookChannel = "table {{.TokenHeader}}\t{{.TokenValue}}"

	pagerDutyChannel = "table {{.ApiKey}}\t{{.RoutingKey}}"

	channelTypeHeader = "Channel Type"

	iconUrlHeader             = "Icon URL"
	usernameHeader            = "Username"
	webhookUrlHeader          = "Webhook URL"
	defaultRecipientsHeader   = "Default Recipients"
	defaultSmtpSettingsHeader = "Default SMTP Settings"
	recipientsHeader          = "Recipients"
	smtpDataHeader            = "SMTP Data"

	httpAuthHeader     = "HTTP Auth Type"
	sendResolvedHeader = "Send Resolved Alert notification"

	passwordHeader   = "Password"
	tokenHeader      = "Token Header"
	tokenValueHeader = "Token Value"

	apiKeyHeader     = "API Key"
	routingKeyHeader = "Routing Key"
)

// Context for alertChannel outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	a util.AlertChannel
}

// NewAlertChannelFormat for formatting output
func NewAlertChannelFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertChannelListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of AlertChannels
func Write(ctx formatter.Context, alertChannels []util.AlertChannel) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of alertChannels into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(alertChannels, "", "  ")
		} else {
			output, err = json.Marshal(alertChannels)
		}

		if err != nil {
			logrus.Errorf("Error marshaling alert channels to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, alertChannel := range alertChannels {
			err := format(&Context{a: alertChannel})
			if err != nil {
				logrus.Debugf("Error rendering alert channel: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewAlertChannelContext(), render)
}

// NewAlertChannelContext creates a new context for rendering alertChannel
func NewAlertChannelContext() *Context {
	alertChannelCtx := Context{}
	alertChannelCtx.Header = formatter.SubHeaderContext{
		"Name":        formatter.NameHeader,
		"UUID":        formatter.UUIDHeader,
		"ChannelType": channelTypeHeader,

		"IconUrl":    iconUrlHeader,
		"Username":   usernameHeader,
		"WebhookUrl": webhookUrlHeader,
		"Recipients": recipientsHeader,
		"SmtpData":   smtpDataHeader,

		"HttpAuthType":    httpAuthHeader,
		"SendResolved":    sendResolvedHeader,
		"WebhookUsername": usernameHeader,
		"Password":        passwordHeader,
		"TokenHeader":     tokenHeader,
		"TokenValue":      tokenValueHeader,

		"ApiKey":     apiKeyHeader,
		"RoutingKey": routingKeyHeader,
	}
	return &alertChannelCtx
}

// UUID fetches AlertChannel UUID
func (c *Context) UUID() string {
	return c.a.GetUuid()
}

// Name fetches AlertChannel Name
func (c *Context) Name() string {
	return c.a.GetName()
}

// ChannelType fetches AlertChannel Channel Type
func (c *Context) ChannelType() string {
	params := c.a.GetParams()
	return params.GetChannelType()
}

// IconUrl fetches AlertChannel Icon URL
func (c *Context) IconUrl() string {
	params := c.a.GetParams()
	return params.GetIconUrl()
}

// Username fetches AlertChannel Username
func (c *Context) Username() string {
	params := c.a.GetParams()
	return params.GetUsername()
}

// WebhookUrl fetches AlertChannel Webhook URL
func (c *Context) WebhookUrl() string {
	params := c.a.GetParams()
	return params.GetWebhookUrl()
}

// Recipients fetches AlertChannel Recipients
func (c *Context) Recipients() string {
	params := c.a.GetParams()
	if params.GetDefaultRecipients() {
		return "Default Recipients"
	}
	recipients := ""
	for i, recipient := range params.GetRecipients() {
		if i == 0 {
			recipients = recipient
		} else {
			recipients = fmt.Sprintf("%s, %s", recipients, recipient)
		}
	}
	return recipients
}

// SmtpData fetches AlertChannel SMTP Data
func (c *Context) SmtpData() string {
	params := c.a.GetParams()
	if params.GetDefaultSmtpSettings() {
		return "Default SMTP Settings"
	}
	data, err := json.MarshalIndent(params.GetSmtpData(), "", "  ")
	if err != nil {
		return "-"
	}
	return string(data)
}

// HttpAuthType fetches AlertChannel HTTP Auth type
func (c *Context) HttpAuthType() string {
	params := c.a.GetParams()
	httpAuth := params.GetHttpAuth()
	return httpAuth.GetType()
}

// WebhookUsername fetches AlertChannel Webhook Username
func (c *Context) WebhookUsername() string {
	params := c.a.GetParams()
	httpAuth := params.GetHttpAuth()
	return httpAuth.GetUsername()
}

// Password fetches AlertChannel Password
func (c *Context) Password() string {
	params := c.a.GetParams()
	httpAuth := params.GetHttpAuth()
	return httpAuth.GetPassword()
}

// TokenHeader fetches AlertChannel Token Header
func (c *Context) TokenHeader() string {
	params := c.a.GetParams()
	httpAuth := params.GetHttpAuth()
	return httpAuth.GetTokenHeader()
}

// TokenValue fetches AlertChannel Token Value
func (c *Context) TokenValue() string {
	params := c.a.GetParams()
	httpAuth := params.GetHttpAuth()
	return httpAuth.GetTokenValue()
}

// SendResolved fetches AlertChannel Send Resolved
func (c *Context) SendResolved() string {
	params := c.a.GetParams()
	return fmt.Sprintf("%t", params.GetSendResolved())
}

// ApiKey fetches AlertChannel API Key
func (c *Context) ApiKey() string {
	params := c.a.GetParams()
	return params.GetApiKey()
}

// RoutingKey fetches AlertChannel Routing Key
func (c *Context) RoutingKey() string {
	params := c.a.GetParams()
	return params.GetRoutingKey()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.a)
}
