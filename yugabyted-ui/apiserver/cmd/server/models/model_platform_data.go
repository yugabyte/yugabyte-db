package models

// PlatformData - Information about YB Platform
type PlatformData struct {

	Hostname string `json:"hostname"`

	CloudInfo CloudInfo `json:"cloud_info"`

	// A CIDR block
	Cidr string `json:"cidr"`

	PlatformType PlatformTypeEnum `json:"platform_type"`

	VpcId string `json:"vpc_id"`

	CertificateAuthority string `json:"certificate_authority"`

	KubeClusterEndpoint string `json:"kube_cluster_endpoint"`

	KubeClusterName string `json:"kube_cluster_name"`
}
