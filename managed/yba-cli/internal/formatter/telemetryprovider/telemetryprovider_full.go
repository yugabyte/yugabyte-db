/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryprovider

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FullTelemetryProviderContext to render TelemetryProvider Listing output
type FullTelemetryProviderContext struct {
	formatter.HeaderContext
	formatter.Context
	tp util.TelemetryProvider
}

// SetFullTelemetryProvider initializes the context with the provider data
func (ftp *FullTelemetryProviderContext) SetFullTelemetryProvider(
	provider util.TelemetryProvider,
) {
	ftp.tp = provider
}

// NewFullTelemetryProviderFormat for formatting output
func NewFullTelemetryProviderFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTelemetryProviderListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullTelemetryProviderContext struct {
	TelemetryProvider *Context
}

// Write populates the output table to be displayed in the command line
func (ftp *FullTelemetryProviderContext) Write() error {
	var err error
	ftpc := &fullTelemetryProviderContext{
		TelemetryProvider: &Context{},
	}
	ftpc.TelemetryProvider.tp = ftp.tp

	// Section 1
	tmpl, err := ftp.startSubsection(defaultTelemetryProviderListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ftp.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	ftp.Output.Write([]byte("\n"))
	if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ftp.PostFormat(tmpl, NewTelemetryProviderContext())

	tmpl, err = ftp.startSubsection(telemetryProvider1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ftp.Output.Write([]byte("\n"))
	if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ftp.PostFormat(tmpl, NewTelemetryProviderContext())

	tmpl, err = ftp.startSubsection(telemetryProvider2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ftp.Output.Write([]byte("\n"))
	if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	ftp.PostFormat(tmpl, NewTelemetryProviderContext())

	config := ftpc.TelemetryProvider.tp.GetConfig()
	providerType := config.GetType()
	switch providerType {
	case util.GCPCloudMonitoringTelemetryProviderType:
		tmpl, err = ftp.startSubsection(gcpType)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.subSection("GCP Cloud Monitoring Telemetry Provider Details")
		if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.PostFormat(tmpl, NewTelemetryProviderContext())
		ftp.Output.Write([]byte("\n"))
	case util.DataDogTelemetryProviderType:
		tmpl, err = ftp.startSubsection(dataDogType)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.subSection("DataDog Telemetry Provider Details")
		if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.PostFormat(tmpl, NewTelemetryProviderContext())
		ftp.Output.Write([]byte("\n"))
	case util.SplunkTelemetryProviderType:
		tmpl, err = ftp.startSubsection(splunkType1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.subSection("Splunk Telemetry Provider Details")
		if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.PostFormat(tmpl, NewTelemetryProviderContext())

		tmpl, err = ftp.startSubsection(splunkType2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.Output.Write([]byte("\n"))
		if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.PostFormat(tmpl, NewTelemetryProviderContext())
		ftp.Output.Write([]byte("\n"))
	case util.AWSCloudWatchTelemetryProviderType:
		tmpl, err = ftp.startSubsection(awsType1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.subSection("AWS CloudWatch Telemetry Provider Details")
		if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.PostFormat(tmpl, NewTelemetryProviderContext())

		tmpl, err = ftp.startSubsection(awsType2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.Output.Write([]byte("\n"))
		if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.PostFormat(tmpl, NewTelemetryProviderContext())

		tmpl, err = ftp.startSubsection(awsType3)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.Output.Write([]byte("\n"))
		if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.PostFormat(tmpl, NewTelemetryProviderContext())
		ftp.Output.Write([]byte("\n"))
	case util.LokiTelemetryProviderType:
		tmpl, err = ftp.startSubsection(lokiType1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.subSection("Loki Telemetry Provider Details")
		if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		ftp.PostFormat(tmpl, NewTelemetryProviderContext())

		authType := config.GetAuthType()
		switch authType {
		case util.BasicLokiAuthType:
			tmpl, err = ftp.startSubsection(lokiType2)
			if err != nil {
				logrus.Errorf("%s", err.Error())
				return err
			}
			ftp.Output.Write([]byte("\n"))
			if err := ftp.ContextFormat(tmpl, ftpc.TelemetryProvider); err != nil {
				logrus.Errorf("%s", err.Error())
				return err
			}
			ftp.PostFormat(tmpl, NewTelemetryProviderContext())
		}

		ftp.Output.Write([]byte("\n"))
	}

	return nil
}

func (ftp *FullTelemetryProviderContext) startSubsection(
	format string,
) (*template.Template, error) {
	ftp.Buffer = bytes.NewBufferString("")
	ftp.ContextHeader = ""
	ftp.Format = formatter.Format(format)
	ftp.PreFormat()

	return ftp.ParseFormat()
}

func (ftp *FullTelemetryProviderContext) subSection(name string) {
	ftp.Output.Write([]byte("\n"))
	ftp.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	ftp.Output.Write([]byte("\n"))
}

// NewFullTelemetryProviderContext creates tp new context for rendering provider
func NewFullTelemetryProviderContext() *FullTelemetryProviderContext {
	providerCtx := FullTelemetryProviderContext{}
	providerCtx.Header = formatter.SubHeaderContext{}
	return &providerCtx
}

// MarshalJSON function
func (ftp *FullTelemetryProviderContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(ftp.tp)
}
