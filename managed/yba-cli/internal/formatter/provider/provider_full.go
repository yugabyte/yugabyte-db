/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/aws"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/azu"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/gcp"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/kubernetes"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem"
)

const (
	// Provider details
	defaultFullProviderGeneral = "table {{.Name}}\t{{.Code}}\t{{.UUID}}\t{{.Status}}"
	providerDetails1           = "table {{.SSHUser}}\t{{.SSHPort}}\t{{.ProvisionInstanceScript}}" +
		"\t{{.SkipProvisioning}}\t{{.KeyPairName}}"
	providerDetails2 = "table {{.EnableNodeAgent}}\t{{.InstallNodeExporter}}" +
		"\t{{.NodeExporterPort}}\t{{.NodeExporterUser}}\t{{.SetUpChrony}}" +
		"\t{{.ShowSetUpChrony}}\t{{.NTPServers}}"

	// Access key Details

)

// FullProviderContext to render Provider Details output
type FullProviderContext struct {
	formatter.HeaderContext
	formatter.Context
	p ybaclient.Provider
}

// SetFullProvider initializes the context with the provider data
func (fp *FullProviderContext) SetFullProvider(provider ybaclient.Provider) {
	fp.p = provider
}

// NewFullProviderFormat for formatting output
func NewFullProviderFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultProviderListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullProviderContext struct {
	Provider       *Context
	AWSProvider    *aws.ProviderContext
	GCPProvider    *gcp.ProviderContext
	AzureProvider  *azu.ProviderContext
	KubeProvider   *kubernetes.ProviderContext
	OnpremProvider *onprem.ProviderContext
}

// Write populates the output table to be displayed in the command line
func (fp *FullProviderContext) Write() error {
	var err error
	fpc := &fullProviderContext{
		Provider: &Context{},
	}
	fpc.Provider.p = fp.p
	details := fp.p.GetDetails()
	cloudInfo := details.GetCloudInfo()
	fpc.AWSProvider = &aws.ProviderContext{
		Aws: cloudInfo.GetAws(),
	}
	fpc.GCPProvider = &gcp.ProviderContext{
		Gcp: cloudInfo.GetGcp(),
	}
	fpc.AzureProvider = &azu.ProviderContext{
		Azu: cloudInfo.GetAzu(),
	}
	fpc.KubeProvider = &kubernetes.ProviderContext{
		Kube: cloudInfo.GetKubernetes(),
	}
	fpc.OnpremProvider = &onprem.ProviderContext{
		Onprem: cloudInfo.GetOnprem(),
	}

	// Section 1
	tmpl, err := fp.startSubsection(defaultFullProviderGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fp.Output.Write([]byte("\n"))
	if err := fp.ContextFormat(tmpl, fpc.Provider); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewProviderContext())
	fp.Output.Write([]byte("\n"))

	// Section 2: Provider Details subSection 1
	tmpl, err = fp.startSubsection(providerDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.subSection("Provider Details")
	if err := fp.ContextFormat(tmpl, fpc.Provider); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewProviderContext())
	fp.Output.Write([]byte("\n"))

	// Section 2: Provider Details subSection 2
	tmpl, err = fp.startSubsection(providerDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fp.ContextFormat(tmpl, fpc.Provider); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fp.PostFormat(tmpl, NewProviderContext())
	fp.Output.Write([]byte("\n"))

	// Cloud Specific subSection
	code := fp.p.GetCode()
	switch code {
	case util.AWSProviderType:
		tmpl, err = fp.startSubsection(aws.Provider)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.subSection("AWS Provider Details")
		if err := fp.ContextFormat(tmpl, fpc.AWSProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.PostFormat(tmpl, aws.NewProviderContext())
	case util.GCPProviderType:
		tmpl, err = fp.startSubsection(gcp.Provider)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.subSection("GCP Provider Details")
		if err := fp.ContextFormat(tmpl, fpc.GCPProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.PostFormat(tmpl, gcp.NewProviderContext())
	case util.AzureProviderType:
		tmpl, err = fp.startSubsection(azu.Provider1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.subSection("Azure Provider Details")
		if err := fp.ContextFormat(tmpl, fpc.AzureProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.PostFormat(tmpl, azu.NewProviderContext())
		fp.Output.Write([]byte("\n"))

		tmpl, err = fp.startSubsection(azu.Provider2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fp.ContextFormat(tmpl, fpc.AzureProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.PostFormat(tmpl, azu.NewProviderContext())
	case util.K8sProviderType:
		tmpl, err = fp.startSubsection(kubernetes.Provider1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.subSection("Kubernetes Provider Details")
		if err := fp.ContextFormat(tmpl, fpc.KubeProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.PostFormat(tmpl, kubernetes.NewProviderContext())
		fp.Output.Write([]byte("\n"))

		tmpl, err = fp.startSubsection(kubernetes.Provider2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := fp.ContextFormat(tmpl, fpc.KubeProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.PostFormat(tmpl, kubernetes.NewProviderContext())
	case util.OnpremProviderType:
		tmpl, err = fp.startSubsection(onprem.Provider)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.subSection("On Premises Provider Details")
		if err := fp.ContextFormat(tmpl, fpc.OnpremProvider); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		fp.PostFormat(tmpl, onprem.NewProviderContext())
	}

	// Regions subSection
	logrus.Debugf("Number of Regions: %d", len(fp.p.GetRegions()))
	fp.subSection("Regions")
	for i, v := range fp.p.GetRegions() {
		regionContext := *NewRegionContext()
		regionContext.Output = os.Stdout
		regionContext.Format = NewFullProviderFormat(viper.GetString("output"))
		regionContext.SetRegion(v)
		regionContext.Write(code, i)
	}

	// ImageBundle subSection
	logrus.Debugf("Number of Linux Versions: %d", len(fp.p.GetImageBundles()))
	fp.subSection("Linux Version Catalog")
	for i, v := range fp.p.GetImageBundles() {
		imageBundleContext := *NewImageBundleContext()
		imageBundleContext.Output = os.Stdout
		imageBundleContext.Format = NewFullProviderFormat(viper.GetString("output"))
		imageBundleContext.SetImageBundle(v)
		imageBundleContext.Write(code, i)
	}

	return nil
}

func (fp *FullProviderContext) startSubsection(format string) (*template.Template, error) {
	fp.Buffer = bytes.NewBufferString("")
	fp.ContextHeader = ""
	fp.Format = formatter.Format(format)
	fp.PreFormat()

	return fp.ParseFormat()
}

func (fp *FullProviderContext) subSection(name string) {
	fp.Output.Write([]byte("\n\n"))
	fp.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fp.Output.Write([]byte("\n"))
}

// NewFullProviderContext creates a new context for rendering provider
func NewFullProviderContext() *FullProviderContext {
	providerCtx := FullProviderContext{}
	providerCtx.Header = formatter.SubHeaderContext{}
	return &providerCtx
}

// MarshalJSON function
func (fp *FullProviderContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fp.p)
}
