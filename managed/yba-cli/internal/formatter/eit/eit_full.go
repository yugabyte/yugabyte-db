/*
 * Copyright (c) YugaByte, Inc.
 */

package eit

import (
	"bytes"
	"encoding/json"
	"os"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/customcertpath"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/customservercert"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/hashicorp"
)

const (
	defaultFullEITGeneral = "table {{.Name}}\t{{.UUID}}\t{{.CertType}}\t{{.InUse}}"

	eitDetails1 = "table {{.CreationDate}}\t{{.ExpirationDate}}"

	eitDetails2 = "table {{.Certificate}}\t{{.PrivateKey}}"
)

// FullEITContext to render EIT Details output
type FullEITContext struct {
	formatter.HeaderContext
	formatter.Context
	eit ybaclient.CertificateInfoExt
}

// SetFullEIT initializes the context with the eit data
func (feit *FullEITContext) SetFullEIT(eit ybaclient.CertificateInfoExt) {
	feit.eit = eit
}

// NewFullEITFormat for formatting output
func NewFullEITFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultEITListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullEITContext struct {
	EIT          *Context
	HashicorpEIT *hashicorp.EITContext
	// This is for util.CustomCertHostPathCertificateType
	CustomCertPathEIT *customcertpath.EITContext
	// This is for util.CustomServerCertCertificateType
	CustomServerCertEIT *customservercert.EITContext
}

// Write populates the output table to be displayed in the command line
func (feit *FullEITContext) Write() error {
	var err error
	feitc := &fullEITContext{
		EIT: &Context{},
	}
	feitc.EIT.eit = feit.eit

	feitc.HashicorpEIT = &hashicorp.EITContext{
		Hashicorp: feit.eit.GetCustomHCPKICertInfo(),
	}

	feitc.CustomCertPathEIT = &customcertpath.EITContext{
		CustomCertPath: feit.eit.GetCustomCertPathParams(),
	}

	feitc.CustomServerCertEIT = &customservercert.EITContext{
		CustomServerCert: feit.eit.GetCustomServerCertInfo(),
	}

	// Section 1
	tmpl, err := feit.startSubsection(defaultFullEITGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	feit.Output.Write([]byte("\n"))
	if err := feit.ContextFormat(tmpl, feitc.EIT); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.PostFormat(tmpl, NewEITContext())
	feit.Output.Write([]byte("\n"))

	// Section 2: EIT Details subSection 1
	tmpl, err = feit.startSubsection(eitDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.subSection("Certificate Details")
	if err := feit.ContextFormat(tmpl, feitc.EIT); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.PostFormat(tmpl, NewEITContext())
	feit.Output.Write([]byte("\n"))

	// Section 2: EIT Details subSection 2
	tmpl, err = feit.startSubsection(eitDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := feit.ContextFormat(tmpl, feitc.EIT); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	feit.PostFormat(tmpl, NewEITContext())
	feit.Output.Write([]byte("\n"))

	certType := feit.eit.GetCertType()
	switch certType {
	case util.CustomCertHostPathCertificateType:
		tmpl, err = feit.startSubsection(customcertpath.EIT1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		feit.subSection("Custom Certificate Details")
		if err := feit.ContextFormat(tmpl, feitc.CustomCertPathEIT); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		feit.PostFormat(tmpl, customcertpath.NewEITContext())
		feit.Output.Write([]byte("\n"))

		tmpl, err = feit.startSubsection(customcertpath.EIT2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := feit.ContextFormat(tmpl, feitc.CustomCertPathEIT); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		feit.PostFormat(tmpl, customcertpath.NewEITContext())
		feit.Output.Write([]byte("\n"))

		tmpl, err = feit.startSubsection(customcertpath.EIT3)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := feit.ContextFormat(tmpl, feitc.CustomCertPathEIT); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		feit.PostFormat(tmpl, customcertpath.NewEITContext())
	case util.CustomServerCertCertificateType:
		tmpl, err = feit.startSubsection(customservercert.EIT)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		feit.subSection("Custom Server Certificate Details")
		if err := feit.ContextFormat(tmpl, feitc.CustomServerCertEIT); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		feit.PostFormat(tmpl, customservercert.NewEITContext())
	case util.HashicorpVaultCertificateType:
		tmpl, err = feit.startSubsection(hashicorp.EIT)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		feit.subSection("Hashicorp Vault Details")
		if err := feit.ContextFormat(tmpl, feitc.HashicorpEIT); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		feit.PostFormat(tmpl, hashicorp.NewEITContext())

	}

	// Associated universes subSection
	logrus.Debugf("Number of Associated Universes: %d", len(feit.eit.GetUniverseDetails()))
	if len(feit.eit.GetUniverseDetails()) > 0 {
		feit.subSection("Associated Universes")
		for i, v := range feit.eit.GetUniverseDetails() {
			associatedUniversesContext := *NewAssociatedUniverseContext()
			associatedUniversesContext.Output = os.Stdout
			associatedUniversesContext.Format = NewFullEITFormat(viper.GetString("output"))
			associatedUniversesContext.SetAssociatedUniverse(v)
			associatedUniversesContext.Write(i)
		}
	}

	return nil
}

func (feit *FullEITContext) startSubsection(format string) (*template.Template, error) {
	feit.Buffer = bytes.NewBufferString("")
	feit.ContextHeader = ""
	feit.Format = formatter.Format(format)
	feit.PreFormat()

	return feit.ParseFormat()
}

func (feit *FullEITContext) subSection(name string) {
	feit.Output.Write([]byte("\n\n"))
	feit.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	feit.Output.Write([]byte("\n"))
}

// NewFullEITContext creates a new context for rendering eit
func NewFullEITContext() *FullEITContext {
	eitCtx := FullEITContext{}
	eitCtx.Header = formatter.SubHeaderContext{}
	return &eitCtx
}

// MarshalJSON function
func (feit *FullEITContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(feit.eit)
}
