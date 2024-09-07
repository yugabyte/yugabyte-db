/*
 * Copyright (c) YugaByte, Inc.
 */

package ear

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/aws"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/azu"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/gcp"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/hashicorp"
)

const (
	defaultFullEARGeneral = "table {{.Name}}\t{{.UUID}}\t{{.KeyProvider}}\t{{.InUse}}"
)

// FullEARContext to render EAR Details output
type FullEARContext struct {
	formatter.HeaderContext
	formatter.Context
	ear util.KMSConfig
}

// SetFullEAR initializes the context with the ear data
func (fear *FullEARContext) SetFullEAR(ear util.KMSConfig) {
	fear.ear = ear
}

// NewFullEARFormat for formatting output
func NewFullEARFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultEARListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullEARContext struct {
	EAR          *Context
	HashicorpEAR *hashicorp.EARContext
	AWSEAR       *aws.EARContext
	GCPEAR       *gcp.EARContext
	AzureEAR     *azu.EARContext
}

// Write populates the output table to be displayed in the command line
func (fear *FullEARContext) Write() error {
	var err error
	fearc := &fullEARContext{
		EAR: &Context{},
	}
	fearc.EAR.ear = fear.ear

	if fear.ear.Hashicorp != nil {
		fearc.HashicorpEAR = &hashicorp.EARContext{
			Hashicorp: *fear.ear.Hashicorp,
		}
	}

	if fear.ear.AWS != nil {
		fearc.AWSEAR = &aws.EARContext{
			Aws: *fear.ear.AWS,
		}
	}

	if fear.ear.Azure != nil {
		fearc.AzureEAR = &azu.EARContext{
			Azu: *fear.ear.Azure,
		}
	}

	if fear.ear.GCP != nil {
		fearc.GCPEAR = &gcp.EARContext{
			Gcp: *fear.ear.GCP,
		}
	}

	// Section 1
	tmpl, err := fear.startSubsection(defaultFullEARGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fear.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fear.Output.Write([]byte("\n"))
	if err := fear.ContextFormat(tmpl, fearc.EAR); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fear.PostFormat(tmpl, NewEARContext())

	keyProviderType := fear.ear.KeyProvider
	switch keyProviderType {
	case util.AWSEARType:
		tmpl, err = fear.startSubsection(aws.EAR1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.subSection("AWS KMS Details")
		if err := fear.ContextFormat(tmpl, fearc.AWSEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, aws.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(aws.EAR2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.AWSEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, aws.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(aws.EAR3)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.AWSEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, aws.NewEARContext())
	case util.AzureEARType:
		tmpl, err = fear.startSubsection(azu.EAR1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.subSection("Azure KMS Details")
		if err := fear.ContextFormat(tmpl, fearc.AzureEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, azu.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(azu.EAR2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.AzureEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, azu.NewEARContext())

	case util.GCPEARType:
		tmpl, err = fear.startSubsection(gcp.EAR1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.subSection("GCP KMS Details")
		if err := fear.ContextFormat(tmpl, fearc.GCPEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, gcp.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(gcp.EAR2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.GCPEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, gcp.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(gcp.EAR3)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.GCPEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, gcp.NewEARContext())

	case util.HashicorpVaultEARType:
		tmpl, err = fear.startSubsection(hashicorp.EAR1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.subSection("Hashicorp Vault KMS Details")
		if err := fear.ContextFormat(tmpl, fearc.HashicorpEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, hashicorp.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(hashicorp.EAR2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.HashicorpEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, hashicorp.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(hashicorp.EAR3)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.HashicorpEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, hashicorp.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(hashicorp.EAR4)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.HashicorpEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, hashicorp.NewEARContext())
		fear.Output.Write([]byte("\n"))

		tmpl, err = fear.startSubsection(hashicorp.EAR5)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fear.ContextFormat(tmpl, fearc.HashicorpEAR); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fear.PostFormat(tmpl, hashicorp.NewEARContext())

	}

	return nil
}

func (fear *FullEARContext) startSubsection(format string) (*template.Template, error) {
	fear.Buffer = bytes.NewBufferString("")
	fear.ContextHeader = ""
	fear.Format = formatter.Format(format)
	fear.PreFormat()

	return fear.ParseFormat()
}

func (fear *FullEARContext) subSection(name string) {
	fear.Output.Write([]byte("\n\n"))
	fear.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fear.Output.Write([]byte("\n"))
}

// NewFullEARContext creates a new context for rendering ear
func NewFullEARContext() *FullEARContext {
	earCtx := FullEARContext{}
	earCtx.Header = formatter.SubHeaderContext{}
	return &earCtx
}

// MarshalJSON function
func (fear *FullEARContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fear.ear)
}
