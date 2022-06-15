package models

// KmsSpec - Spec to create the KMS Config
type KmsSpec struct {

	// Name of the KMS config
	Name string `json:"name"`

	// AWS access key
	AccessKey string `json:"access_key"`

	// AWS secret key
	SecretKey string `json:"secret_key"`

	// CMK id of AWS KMS
	CmkId string `json:"cmk_id"`

	// AWS region
	Region string `json:"region"`

	// KMS endpoint
	KmsEndpoint string `json:"kms_endpoint"`
}
