/*
 * Copyright (c) YugaByte, Inc.
 */

package kubernetes

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// Provider1 provides header for K8S Cloud Info
	Provider1 = "table {{.KubeConfig}}\t{{.KubeConfigContent}}\t{{.KubeConfigName}}" +
		"\t{{.KubernetesImagePullSecretName}}\t{{.KubernetesImageRegistry}}"
	// Provider2 provides header for K8S Cloud Info
	Provider2 = "table {{.KubernetesProvider}}\t{{.KubernetesPullSecret}}" +
		"\t{{.KubernetesPullSecretName}}\t{{.KubernetesStorageClass}}"

	// Region1 provides header for K8S Region Cloud Info
	Region1 = "table {{.CertManagerClusterIssuer}}\t{{.CertManagerIssuer}}"
	// Region2 provides header for K8S Region Cloud Info
	Region2 = "table {{.KubeConfig}}\t{{.KubeConfigContent}}\t{{.KubeConfigName}}\t{{.KubeDomain}}"
	// Region3 provides header for K8S Region Cloud Info
	Region3 = "table {{.KubeNamespace}}\t{{.KubePodAddressTemplate}}" +
		"\t{{.KubernetesImagePullSecretName}} "
	// Region4 provides header for K8S Region Cloud Info
	Region4 = "table {{.KubernetesImageRegistry}}\t{{.KubernetesProvider}}\t{{.KubernetesPullSecret}}"
	// Region5 provides header for K8S Region Cloud Info
	Region5 = "table {{.KubernetesPullSecretName}}\t{{.KubernetesStorageClass}}"
	// Region6 provides header for K8S Region Cloud Info
	Region6 = "table {{.Overrides}}"

	certManagerClusterIssuerHeader = "Certificate Manager Cluster Issuer"
	certManagerIssuerHeader        = "Certificate Manager Issuer"
	kubeConfigHeader               = "Config"
	kubeConfigContentHeader        = "Config Content"
	kubeConfigNameHeader           = "Config Name"
	kubeDomainHeader               = "Domain"
	kubeNamespaceHeader            = "Namespace"
	kubePodAddressTemplateHeader   = "Pod Address Template"
	kubeImagePullSecretNameHeader  = "Image Pull Secret Name"
	kubeImageRegistryHeader        = "Image Registry"
	kubeProviderHeader             = "Provider"
	kubePullSecretHeader           = "Pull Secret"
	kubePullSecretNameHeader       = "Pull Secret Name"
	kubeStorageClassHeader         = "Storage Class"
	kubeOverridesHeader            = "Overrides"
)

// ProviderContext for provider outputs
type ProviderContext struct {
	formatter.HeaderContext
	formatter.Context
	Kube ybaclient.KubernetesInfo
}

// RegionContext for provider outputs
type RegionContext struct {
	formatter.HeaderContext
	formatter.Context
	Region ybaclient.KubernetesRegionInfo
}

// ZoneContext for provider outputs
type ZoneContext struct {
	formatter.HeaderContext
	formatter.Context
	Zone ybaclient.KubernetesRegionInfo
}

// NewProviderFormat for formatting output
func NewProviderFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := Provider1
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewRegionFormat for formatting output
func NewRegionFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := Region1
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewZoneFormat for formatting output
func NewZoneFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := Region1
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewProviderContext creates a new context for rendering provider
func NewProviderContext() *ProviderContext {
	kubeProviderCtx := ProviderContext{}
	kubeProviderCtx.Header = formatter.SubHeaderContext{
		"KubeConfig":                    kubeConfigHeader,
		"KubeConfigContent":             kubeConfigContentHeader,
		"KubeConfigName":                kubeConfigNameHeader,
		"KubernetesImagePullSecretName": kubeImagePullSecretNameHeader,
		"KubernetesImageRegistry":       kubeImageRegistryHeader,
		"KubernetesProvider":            kubeProviderHeader,
		"KubernetesPullSecret":          kubePullSecretHeader,
		"KubernetesPullSecretName":      kubePullSecretNameHeader,
		"KubernetesStorageClass":        kubeStorageClassHeader,
	}
	return &kubeProviderCtx
}

// NewRegionContext creates a new context for rendering provider
func NewRegionContext() *RegionContext {
	kubeRegionCtx := RegionContext{}
	kubeRegionCtx.Header = formatter.SubHeaderContext{
		"CertManagerClusterIssuer":      certManagerClusterIssuerHeader,
		"CertManagerIssuer":             certManagerIssuerHeader,
		"KubeConfig":                    kubeConfigHeader,
		"KubeConfigContent":             kubeConfigContentHeader,
		"KubeConfigName":                kubeConfigNameHeader,
		"KubeDomain":                    kubeDomainHeader,
		"KubeNamespace":                 kubeNamespaceHeader,
		"KubePodAddressTemplate":        kubePodAddressTemplateHeader,
		"KubernetesImagePullSecretName": kubeImagePullSecretNameHeader,
		"KubernetesImageRegistry":       kubeImageRegistryHeader,
		"KubernetesProvider":            kubeProviderHeader,
		"KubernetesPullSecret":          kubePullSecretHeader,
		"KubernetesPullSecretName":      kubePullSecretNameHeader,
		"KubernetesStorageClass":        kubeStorageClassHeader,
		"Overrides":                     kubeOverridesHeader,
	}
	return &kubeRegionCtx
}

// NewZoneContext creates a new context for rendering provider
func NewZoneContext() *ZoneContext {
	kubeZoneCtx := ZoneContext{}
	kubeZoneCtx.Header = formatter.SubHeaderContext{
		"CertManagerClusterIssuer":      certManagerClusterIssuerHeader,
		"CertManagerIssuer":             certManagerIssuerHeader,
		"KubeConfig":                    kubeConfigHeader,
		"KubeConfigContent":             kubeConfigContentHeader,
		"KubeConfigName":                kubeConfigNameHeader,
		"KubeDomain":                    kubeDomainHeader,
		"KubeNamespace":                 kubeNamespaceHeader,
		"KubePodAddressTemplate":        kubePodAddressTemplateHeader,
		"KubernetesImagePullSecretName": kubeImagePullSecretNameHeader,
		"KubernetesImageRegistry":       kubeImageRegistryHeader,
		"KubernetesProvider":            kubeProviderHeader,
		"KubernetesPullSecret":          kubePullSecretHeader,
		"KubernetesPullSecretName":      kubePullSecretNameHeader,
		"KubernetesStorageClass":        kubeStorageClassHeader,
		"Overrides":                     kubeOverridesHeader,
	}
	return &kubeZoneCtx
}

// KubeConfig returns the KubeConfig field.
func (c *ProviderContext) KubeConfig() string {
	return c.Kube.GetKubeConfig()
}

// KubeConfigContent returns the KubeConfigContent field.
func (c *ProviderContext) KubeConfigContent() string {
	return c.Kube.GetKubeConfigContent()
}

// KubeConfigName returns the KubeConfigName field.
func (c *ProviderContext) KubeConfigName() string {
	return c.Kube.GetKubeConfigName()
}

// KubernetesImagePullSecretName returns the KubernetesImagePullSecretName field.
func (c *ProviderContext) KubernetesImagePullSecretName() string {
	return c.Kube.GetKubernetesImagePullSecretName()
}

// KubernetesImageRegistry returns the KubernetesImageRegistry field.
func (c *ProviderContext) KubernetesImageRegistry() string {
	return c.Kube.GetKubernetesImageRegistry()
}

// KubernetesProvider returns the KubernetesProvider field.
func (c *ProviderContext) KubernetesProvider() string {
	return c.Kube.GetKubernetesProvider()
}

// KubernetesPullSecret returns the KubernetesPullSecret field.
func (c *ProviderContext) KubernetesPullSecret() string {
	return c.Kube.GetKubernetesPullSecret()
}

// KubernetesPullSecretName returns the KubernetesPullSecretName field.
func (c *ProviderContext) KubernetesPullSecretName() string {
	return c.Kube.GetKubernetesPullSecretName()
}

// KubernetesStorageClass returns the KubernetesStorageClass field.
func (c *ProviderContext) KubernetesStorageClass() string {
	return c.Kube.GetKubernetesStorageClass()
}

// MarshalJSON function
func (c *ProviderContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Kube)
}

// CertManagerClusterIssuer returns the CertManagerClusterIssuer field.
func (c *RegionContext) CertManagerClusterIssuer() string {
	return c.Region.GetCertManagerClusterIssuer()
}

// CertManagerIssuer returns the CertManagerIssuer field.
func (c *RegionContext) CertManagerIssuer() string {
	return c.Region.GetCertManagerIssuer()
}

// KubeConfig returns the KubeConfig field.
func (c *RegionContext) KubeConfig() string {
	return c.Region.GetKubeConfig()
}

// KubeConfigContent returns the KubeConfigContent field.
func (c *RegionContext) KubeConfigContent() string {
	return c.Region.GetKubeConfigContent()
}

// KubeConfigName returns the KubeConfigName field.
func (c *RegionContext) KubeConfigName() string {
	return c.Region.GetKubeConfigName()
}

// KubeDomain returns the KubeDomain field.
func (c *RegionContext) KubeDomain() string {
	return c.Region.GetKubeDomain()
}

// KubeNamespace returns the KubeNamespace field.
func (c *RegionContext) KubeNamespace() string {
	return c.Region.GetKubeNamespace()
}

// KubePodAddressTemplate returns the KubePodAddressTemplate field.
func (c *RegionContext) KubePodAddressTemplate() string {
	return c.Region.GetKubePodAddressTemplate()
}

// KubernetesImagePullSecretName returns the KubernetesImagePullSecretName field.
func (c *RegionContext) KubernetesImagePullSecretName() string {
	return c.Region.GetKubernetesImagePullSecretName()
}

// KubernetesImageRegistry returns the KubernetesImageRegistry field.
func (c *RegionContext) KubernetesImageRegistry() string {
	return c.Region.GetKubernetesImageRegistry()
}

// KubernetesProvider returns the KubernetesProvider field.
func (c *RegionContext) KubernetesProvider() string {
	return c.Region.GetKubernetesProvider()
}

// KubernetesPullSecret returns the KubernetesPullSecret field.
func (c *RegionContext) KubernetesPullSecret() string {
	return c.Region.GetKubernetesPullSecret()
}

// KubernetesPullSecretName returns the KubernetesPullSecretName field.
func (c *RegionContext) KubernetesPullSecretName() string {
	return c.Region.GetKubernetesPullSecretName()
}

// KubernetesStorageClass returns the KubernetesStorageClass field.
func (c *RegionContext) KubernetesStorageClass() string {
	return c.Region.GetKubernetesStorageClass()
}

// Overrides returns the Overrides field.
func (c *RegionContext) Overrides() string {
	return c.Region.GetOverrides()
}

// MarshalJSON function
func (c *RegionContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Region)
}

// CertManagerClusterIssuer returns the CertManagerClusterIssuer field.
func (c *ZoneContext) CertManagerClusterIssuer() string {
	return c.Zone.GetCertManagerClusterIssuer()
}

// CertManagerIssuer returns the CertManagerIssuer field.
func (c *ZoneContext) CertManagerIssuer() string {
	return c.Zone.GetCertManagerIssuer()
}

// KubeConfig returns the KubeConfig field.
func (c *ZoneContext) KubeConfig() string {
	return c.Zone.GetKubeConfig()
}

// KubeConfigContent returns the KubeConfigContent field.
func (c *ZoneContext) KubeConfigContent() string {
	return c.Zone.GetKubeConfigContent()
}

// KubeConfigName returns the KubeConfigName field.
func (c *ZoneContext) KubeConfigName() string {
	return c.Zone.GetKubeConfigName()
}

// KubeDomain returns the KubeDomain field.
func (c *ZoneContext) KubeDomain() string {
	return c.Zone.GetKubeDomain()
}

// KubeNamespace returns the KubeNamespace field.
func (c *ZoneContext) KubeNamespace() string {
	return c.Zone.GetKubeNamespace()
}

// KubePodAddressTemplate returns the KubePodAddressTemplate field.
func (c *ZoneContext) KubePodAddressTemplate() string {
	return c.Zone.GetKubePodAddressTemplate()
}

// KubernetesImagePullSecretName returns the KubernetesImagePullSecretName field.
func (c *ZoneContext) KubernetesImagePullSecretName() string {
	return c.Zone.GetKubernetesImagePullSecretName()
}

// KubernetesImageRegistry returns the KubernetesImageRegistry field.
func (c *ZoneContext) KubernetesImageRegistry() string {
	return c.Zone.GetKubernetesImageRegistry()
}

// KubernetesProvider returns the KubernetesProvider field.
func (c *ZoneContext) KubernetesProvider() string {
	return c.Zone.GetKubernetesProvider()
}

// KubernetesPullSecret returns the KubernetesPullSecret field.
func (c *ZoneContext) KubernetesPullSecret() string {
	return c.Zone.GetKubernetesPullSecret()
}

// KubernetesPullSecretName returns the KubernetesPullSecretName field.
func (c *ZoneContext) KubernetesPullSecretName() string {
	return c.Zone.GetKubernetesPullSecretName()
}

// KubernetesStorageClass returns the KubernetesStorageClass field.
func (c *ZoneContext) KubernetesStorageClass() string {
	return c.Zone.GetKubernetesStorageClass()
}

// Overrides returns the Overrides field.
func (c *ZoneContext) Overrides() string {
	return c.Zone.GetOverrides()
}

// MarshalJSON function
func (c *ZoneContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Zone)
}
