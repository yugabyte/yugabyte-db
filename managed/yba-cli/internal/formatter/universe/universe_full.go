/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

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
)

const (
	defaultFullUniverseGeneral = "table {{.Name}}\t{{.UUID}}\t{{.Version}}\t{{.State}}"
	universeGeneral1           = "table {{.ProviderCode}}\t{{.RF}}" +
		"\t{{.NumMasters}}\t{{.NumTservers}}\t{{.LiveNodes}}"
	universeDetails1 = "table  {{.ProviderUUID}}\t{{.AccessKey}}\t{{.PricePerDay}}" +
		"\t{{.CPUArchitecture}}"
	universeDetails2 = "table {{.EnableYSQL}}\t{{.YSQLAuthEnabled}}\t{{.EnableYCQL}}" +
		"\t{{.YCQLAuthEnabled}}\t{{.UseSystemd}}"
	encryptionRestDetails    = "table {{.KMSEnabled}}\t{{.KMSConfigName}}\t{{.EncryptionRestType}}"
	encryptionTransitDetails = "table {{.NtoNTLS}}\t{{.NtoNCert}}\t{{.CtoNTLS}}\t{{.CtoNCert}}"
	universeOverridesTable   = "table {{.UniverseOverrides}}"
	azOverridesTable         = "table {{.AZOverrides}}"
)

// Certificates hold certificates declared under the current customer
var Certificates []ybaclient.CertificateInfoExt

// Providers hold providers declared under the current customer
var Providers []ybaclient.Provider

// KMSConfigs hold kms configs declared under the current customer
var KMSConfigs []map[string]interface{}

// FullUniverseContext to render Universe Details output
type FullUniverseContext struct {
	formatter.HeaderContext
	formatter.Context
	u ybaclient.UniverseResp
}

// SetFullUniverse initializes the context with the universe data
func (fu *FullUniverseContext) SetFullUniverse(universe ybaclient.UniverseResp) {
	fu.u = universe
}

// NewFullUniverseFormat for formatting output
func NewFullUniverseFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultUniverseListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullUniverseContext struct {
	Universe *Context
}

// Write populates the output table to be displayed in the command line
func (fu *FullUniverseContext) Write() error {
	var err error
	fuc := &fullUniverseContext{
		Universe: &Context{},
	}
	fuc.Universe.u = fu.u

	// Section 1
	tmpl, err := fu.startSubsection(defaultFullUniverseGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fu.Output.Write([]byte("\n"))
	if err := fu.ContextFormat(tmpl, fuc.Universe); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUniverseContext())
	fu.Output.Write([]byte("\n"))

	// Provider information
	tmpl, err = fu.startSubsection(universeGeneral1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fu.ContextFormat(tmpl, fuc.Universe); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUniverseContext())
	fu.Output.Write([]byte("\n"))

	// Section 2: Universe Details subSection 1
	tmpl, err = fu.startSubsection(universeDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.subSection("Universe Details")
	if err := fu.ContextFormat(tmpl, fuc.Universe); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUniverseContext())
	fu.Output.Write([]byte("\n"))

	tmpl, err = fu.startSubsection(universeDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fu.ContextFormat(tmpl, fuc.Universe); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUniverseContext())
	fu.Output.Write([]byte("\n"))

	// Section 3: encryption section
	tmpl, err = fu.startSubsection(encryptionTransitDetails)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.subSection("Encryption In Transit Details")
	if err := fu.ContextFormat(tmpl, fuc.Universe); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUniverseContext())
	fu.Output.Write([]byte("\n"))

	// Section 4: encryption at rest section
	tmpl, err = fu.startSubsection(encryptionRestDetails)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.subSection("Encryption At Rest Details")
	if err := fu.ContextFormat(tmpl, fuc.Universe); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUniverseContext())
	fu.Output.Write([]byte("\n"))

	details := fu.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	if userIntent.GetProviderType() == util.K8sProviderType {
		// Section 5: Overrides for kubernetes
		tmpl, err = fu.startSubsection(universeOverridesTable)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fu.subSection("Helm Overrides")
		if err := fu.ContextFormat(tmpl, fuc.Universe); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fu.PostFormat(tmpl, NewUniverseContext())
		fu.Output.Write([]byte("\n"))

		tmpl, err = fu.startSubsection(azOverridesTable)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fu.ContextFormat(tmpl, fuc.Universe); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fu.PostFormat(tmpl, NewUniverseContext())
	}

	// Clusters subSection

	logrus.Debugf("Number of Clusters: %d", len(details.GetClusters()))
	fu.subSection("Clusters")
	for i, v := range details.GetClusters() {
		clusterContext := *NewClusterContext()
		clusterContext.Output = os.Stdout
		clusterContext.Format = NewFullUniverseFormat(viper.GetString("output"))
		clusterContext.SetCluster(v)
		clusterContext.Write(i)
		fu.Output.Write([]byte("\n"))
	}

	return nil
}

func (fu *FullUniverseContext) startSubsection(format string) (*template.Template, error) {
	fu.Buffer = bytes.NewBufferString("")
	fu.ContextHeader = ""
	fu.Format = formatter.Format(format)
	fu.PreFormat()

	return fu.ParseFormat()
}

func (fu *FullUniverseContext) subSection(name string) {
	fu.Output.Write([]byte("\n\n"))
	fu.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fu.Output.Write([]byte("\n"))
}

// NewFullUniverseContext creates a new context for rendering universe
func NewFullUniverseContext() *FullUniverseContext {
	universeCtx := FullUniverseContext{}
	universeCtx.Header = formatter.SubHeaderContext{}
	return &universeCtx
}

// MarshalJSON function
func (fu *FullUniverseContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fu.u)
}
