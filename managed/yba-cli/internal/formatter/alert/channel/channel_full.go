/*
 * Copyright (c) YugaByte, Inc.
 */

package channel

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullAlertChannelContext to render AlertChannel Listing output
type FullAlertChannelContext struct {
	formatter.HeaderContext
	formatter.Context
	a util.AlertChannel
}

// SetFullAlertChannel initializes the context with the alert data
func (fa *FullAlertChannelContext) SetFullAlertChannel(
	alert util.AlertChannel,
) {
	fa.a = alert
}

// NewFullAlertChannelFormat for formatting output
func NewFullAlertChannelFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertChannelListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullAlertChannelContext struct {
	AlertChannel *Context
}

// Write populates the output table to be displayed in the command line
func (fa *FullAlertChannelContext) Write() error {
	var err error
	fac := &fullAlertChannelContext{
		AlertChannel: &Context{},
	}
	fac.AlertChannel.a = fa.a

	// Section 1
	tmpl, err := fa.startSubsection(defaultAlertChannelListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fa.Output.Write([]byte("\n"))
	if err := fa.ContextFormat(tmpl, fac.AlertChannel); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fa.PostFormat(tmpl, NewAlertChannelContext())

	params := fac.AlertChannel.a.GetParams()
	channelType := params.GetChannelType()
	switch channelType {
	case util.SlackAlertChannelType:
		tmpl, err = fa.startSubsection(slackChannel)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.subSection("Slack Alert Channel Details")
		if err := fa.ContextFormat(tmpl, fac.AlertChannel); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.PostFormat(tmpl, NewAlertChannelContext())
	case util.EmailAlertChannelType:
		tmpl, err = fa.startSubsection(emailChannel1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.subSection("Email Alert Channel Details")
		if err := fa.ContextFormat(tmpl, fac.AlertChannel); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.PostFormat(tmpl, NewAlertChannelContext())
		fa.Output.Write([]byte("\n"))

		tmpl, err = fa.startSubsection(emailChannel2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fa.ContextFormat(tmpl, fac.AlertChannel); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.PostFormat(tmpl, NewAlertChannelContext())
	case util.WebhookAlertChannelType:
		tmpl, err = fa.startSubsection(webhookChannel)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.subSection("WebHook Alert Channel Details")
		if err := fa.ContextFormat(tmpl, fac.AlertChannel); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.PostFormat(tmpl, NewAlertChannelContext())

		httpAuth := params.GetHttpAuth()
		switch httpAuth.GetType() {
		case util.BasicHttpAuthType:
			fa.Output.Write([]byte("\n"))
			tmpl, err = fa.startSubsection(basicWebhookChannel)
			if err != nil {
				logrus.Errorf("%s", err.Error())
				return err
			}
			if err := fa.ContextFormat(tmpl, fac.AlertChannel); err != nil {
				logrus.Errorf("%s", err.Error())
				return err
			}
			fa.PostFormat(tmpl, NewAlertChannelContext())
		case util.TokenHttpAuthType:
			fa.Output.Write([]byte("\n"))
			tmpl, err = fa.startSubsection(tokenWebhookChannel)
			if err != nil {
				logrus.Errorf("%s", err.Error())
				return err
			}
			if err := fa.ContextFormat(tmpl, fac.AlertChannel); err != nil {
				logrus.Errorf("%s", err.Error())
				return err
			}
			fa.PostFormat(tmpl, NewAlertChannelContext())
		}
	case util.PagerDutyAlertChannelType:
		tmpl, err = fa.startSubsection(pagerDutyChannel)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.subSection("PagerDuty Alert Channel Details")
		if err := fa.ContextFormat(tmpl, fac.AlertChannel); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fa.PostFormat(tmpl, NewAlertChannelContext())
	}

	return nil
}

func (fa *FullAlertChannelContext) startSubsection(
	format string,
) (*template.Template, error) {
	fa.Buffer = bytes.NewBufferString("")
	fa.ContextHeader = ""
	fa.Format = formatter.Format(format)
	fa.PreFormat()

	return fa.ParseFormat()
}

func (fa *FullAlertChannelContext) subSection(name string) {
	fa.Output.Write([]byte("\n"))
	fa.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fa.Output.Write([]byte("\n"))
}

// NewFullAlertChannelContext creates a new context for rendering alert
func NewFullAlertChannelContext() *FullAlertChannelContext {
	alertCtx := FullAlertChannelContext{}
	alertCtx.Header = formatter.SubHeaderContext{}
	return &alertCtx
}

// MarshalJSON function
func (fa *FullAlertChannelContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fa.a)
}
