/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// StorageConfig details
	defaultFullStorageConfigGeneral = "table {{.Name}}\t{{.Code}}\t{{.UUID}}" +
		"\t{{.BackupLocation}}\t{{.Status}}"

	awsStorageConfig  = "table {{.S3AccessKeyID}}\t{{.S3SecretAccessKey}}\t{{.IAMInstanceProfile}}"
	azStorageConfig   = "table {{.AzSASToken}}"
	gcsStorageConfig1 = "table {{.UseGCPIAM}}"
)

// FullStorageConfigContext to render StorageConfig Details output
type FullStorageConfigContext struct {
	formatter.HeaderContext
	formatter.Context
	s ybaclient.CustomerConfigUI
}

// SetFullStorageConfig initializes the context with the storageConfig data
func (fs *FullStorageConfigContext) SetFullStorageConfig(storageConfig ybaclient.CustomerConfigUI) {
	fs.s = storageConfig
}

// NewFullStorageConfigFormat for formatting output
func NewFullStorageConfigFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultStorageConfigListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullStorageConfigContext struct {
	StorageConfig *Context
}

// Write populates the output table to be displayed in the command line
func (fs *FullStorageConfigContext) Write() error {
	var err error
	fsc := &fullStorageConfigContext{
		StorageConfig: &Context{},
	}
	fsc.StorageConfig.s = fs.s

	// Section 1
	tmpl, err := fs.startSubsection(defaultFullStorageConfigGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fs.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fs.Output.Write([]byte("\n"))
	if err := fs.ContextFormat(tmpl, fsc.StorageConfig); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fs.PostFormat(tmpl, NewStorageConfigContext())
	fs.Output.Write([]byte("\n"))

	// Cloud Specific subSection
	code := fs.s.GetName()
	switch code {
	case util.S3StorageConfigType:
		tmpl, err = fs.startSubsection(awsStorageConfig)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fs.subSection("S3 Details")
		if err := fs.ContextFormat(tmpl, fsc.StorageConfig); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fs.PostFormat(tmpl, NewStorageConfigContext())
		fs.Output.Write([]byte("\n"))

	case util.GCSStorageConfigType:
		tmpl, err = fs.startSubsection(gcsStorageConfig1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fs.subSection("GCS Details")
		if err := fs.ContextFormat(tmpl, fsc.StorageConfig); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fs.PostFormat(tmpl, NewStorageConfigContext())
		fs.Output.Write([]byte("\n"))

	case util.AzureStorageConfigType:
		tmpl, err = fs.startSubsection(azStorageConfig)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fs.subSection("Azure Storage Details")
		if err := fs.ContextFormat(tmpl, fsc.StorageConfig); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fs.PostFormat(tmpl, NewStorageConfigContext())
		fs.Output.Write([]byte("\n"))

	}

	return nil
}

func (fs *FullStorageConfigContext) startSubsection(format string) (*template.Template, error) {
	fs.Buffer = bytes.NewBufferString("")
	fs.ContextHeader = ""
	fs.Format = formatter.Format(format)
	fs.PreFormat()

	return fs.ParseFormat()
}

func (fs *FullStorageConfigContext) subSection(name string) {
	fs.Output.Write([]byte("\n\n"))
	fs.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fs.Output.Write([]byte("\n"))
}

// NewFullStorageConfigContext creates a new context for rendering storageConfig
func NewFullStorageConfigContext() *FullStorageConfigContext {
	storageConfigCtx := FullStorageConfigContext{}
	storageConfigCtx.Header = formatter.SubHeaderContext{}
	return &storageConfigCtx
}

// MarshalJSON function
func (fs *FullStorageConfigContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fs.s)
}
