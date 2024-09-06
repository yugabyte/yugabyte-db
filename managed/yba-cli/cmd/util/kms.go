/*
 * Copyright (c) YugaByte, Inc.
 */

package util

import (
	"strings"

	"golang.org/x/exp/slices"
)

// KMSConfig is a struct to hold values retrieved by parsing the KMS config map
type KMSConfig struct {
	ConfigUUID   string                  `json:"config_uuid"`
	Name         string                  `json:"name"`
	CustomerUUID string                  `json:"customer_uuid,omitempty"`
	KeyProvider  string                  `json:"provider"`
	InUse        bool                    `json:"in_use"`
	Version      int                     `json:"version,omitempty"`
	AWS          *AwsKmsAuthConfigField  `json:"aws,omitempty"`
	GCP          *GcpKmsAuthConfigField  `json:"gcp,omitempty"`
	Azure        *AzuKmsAuthConfigField  `json:"azure,omitempty"`
	Hashicorp    *HcVaultAuthConfigField `json:"hashicorp,omitempty"`
}

// AwsKmsAuthConfigField is a struct to hold values retrieved by parsing the AWS KMS config map
type AwsKmsAuthConfigField struct {
	AccessKeyID     string `json:"AWS_ACCESS_KEY_ID"`
	SecretAccessKey string `json:"AWS_SECRET_ACCESS_KEY"`
	EndPoint        string `json:"AWS_KMS_ENDPOINT"`
	Region          string `json:"AWS_REGION"`
	CMKPolicy       string `json:"cmk_policy"`
	CMKID           string `json:"cmk_id"`
}

// GcpKmsAuthConfigField is a struct to hold values retrieved by parsing the GCP KMS config map
type GcpKmsAuthConfigField struct {
	GCPConfig       map[string]interface{} `json:"GCP_CONFIG"`
	LocationID      string                 `json:"LOCATION_ID"`
	ProtectionLevel string                 `json:"PROTECTION_LEVEL"`
	GcpKmsEndpoint  string                 `json:"GCP_KMS_ENDPOINT"`
	KeyRingID       string                 `json:"KEY_RING_ID"`
	CryptoKeyID     string                 `json:"CRYPTO_KEY_ID"`
}

// AzuKmsAuthConfigField is a struct to hold values retrieved by parsing the Azure KMS config map
type AzuKmsAuthConfigField struct {
	ClientID        string `json:"CLIENT_ID"`
	ClientSecret    string `json:"CLIENT_SECRET"`
	TenantID        string `json:"TENANT_ID"`
	AzuVaultURL     string `json:"AZU_VAULT_URL"`
	AzuKeyName      string `json:"AZU_KEY_NAME"`
	AzuKeyAlgorithm string `json:"AZU_KEY_ALGORITHM"`
	AzuKeySize      int    `json:"AZU_KEY_SIZE"`
}

// HcVaultAuthConfigField is a struct to hold values related to Hashicorp Vault configuration.
type HcVaultAuthConfigField struct {
	HcVaultToken     string `json:"HC_VAULT_TOKEN"`
	HcVaultAddress   string `json:"HC_VAULT_ADDRESS"`
	HcVaultEngine    string `json:"HC_VAULT_ENGINE"`
	HcVaultMountPath string `json:"HC_VAULT_MOUNT_PATH"`
	HcVaultKeyName   string `json:"HC_VAULT_KEY_NAME"`

	HcVaultRoleID        string `json:"HC_VAULT_ROLE_ID"`
	HcVaultSecretID      string `json:"HC_VAULT_SECRET_ID"`
	HcVaultAuthNamespace string `json:"HC_VAULT_AUTH_NAMESPACE"`

	HcVaultPkiRole string `json:"HC_VAULT_PKI_ROLE"`

	HcVaultTTL       int64 `json:"HC_VAULT_TTL"`
	HcVaultTTLExpiry int64 `json:"HC_VAULT_TTL_EXPIRY"`
}

// ConvertToKMSConfig converts the kms config map to KMSConfig struct
func ConvertToKMSConfig(r map[string]interface{}) (KMSConfig, error) {

	var kmsConfig KMSConfig

	// Handle metadata fields
	if metadata, ok := r["metadata"].(map[string]interface{}); ok {
		if configUUID, ok := metadata["configUUID"].(string); ok {
			kmsConfig.ConfigUUID = configUUID
		}
		if name, ok := metadata["name"].(string); ok {
			kmsConfig.Name = name
		}
		if provider, ok := metadata["provider"].(string); ok {
			kmsConfig.KeyProvider = provider
		}
		if inUse, ok := metadata["in_use"].(bool); ok {
			kmsConfig.InUse = inUse
		}
		if version, ok := metadata["version"].(int); ok {
			kmsConfig.Version = version
		}
		if customerUUID, ok := metadata["customerUUID"].(string); ok {
			kmsConfig.CustomerUUID = customerUUID
		}
	}

	// Handle credentials for AWS
	if credentials, ok := r["credentials"].(map[string]interface{}); ok {
		switch kmsConfig.KeyProvider {
		case AWSEARType:
			aws := AwsKmsAuthConfigField{}
			if accessKeyID, ok := credentials[AWSAccessKeyEnv].(string); ok {
				aws.AccessKeyID = accessKeyID
			}
			if secretAccessKey, ok := credentials[AWSSecretAccessKeyEnv].(string); ok {
				aws.SecretAccessKey = secretAccessKey
			}
			if region, ok := credentials[AWSRegionEnv].(string); ok {
				aws.Region = region
			}
			if cmkID, ok := credentials[AWSCMKIDField].(string); ok {
				aws.CMKID = cmkID
			}
			if cmkPolicy, ok := credentials[AWSCMKPolicyField].(string); ok {
				aws.CMKPolicy = cmkPolicy
			}
			if endPoint, ok := credentials[AWSEndpointEnv].(string); ok {
				aws.EndPoint = endPoint
			}
			kmsConfig.AWS = &aws

		case GCPEARType:
			gcp := GcpKmsAuthConfigField{}
			if gcpConfig, ok := credentials[GCPConfigField].(map[string]interface{}); ok {
				gcp.GCPConfig = gcpConfig
			}
			if locationID, ok := credentials[GCPLocationIDField].(string); ok {
				gcp.LocationID = locationID
			}
			if protectionLevel, ok := credentials[GCPProtectionLevelField].(string); ok {
				gcp.ProtectionLevel = protectionLevel
			}
			if gcpKmsEndpoint, ok := credentials[GCPKmsEndpointField].(string); ok {
				gcp.GcpKmsEndpoint = gcpKmsEndpoint
			}
			if keyRingID, ok := credentials[GCPKeyRingIDField].(string); ok {
				gcp.KeyRingID = keyRingID
			}
			if cryptoKeyID, ok := credentials[GCPCryptoKeyIDField].(string); ok {
				gcp.CryptoKeyID = cryptoKeyID
			}
			kmsConfig.GCP = &gcp
		case AzureEARType:
			azure := AzuKmsAuthConfigField{}
			if clientID, ok := credentials[AzureClientIDField].(string); ok {
				azure.ClientID = clientID
			}
			if clientSecret, ok := credentials[AzureClientSecretField].(string); ok {
				azure.ClientSecret = clientSecret
			}
			if tenantID, ok := credentials[AzureTenantIDField].(string); ok {
				azure.TenantID = tenantID
			}
			if azuVaultURL, ok := credentials[AzureVaultURLField].(string); ok {
				azure.AzuVaultURL = azuVaultURL
			}
			if azuKeyName, ok := credentials[AzureKeyNameField].(string); ok {
				azure.AzuKeyName = azuKeyName
			}
			if azuKeyAlgorithm, ok := credentials[AzureKeyAlgorithmField].(string); ok {
				azure.AzuKeyAlgorithm = azuKeyAlgorithm
			}
			if azuKeySize, ok := credentials[AzureKeySizeField].(int); ok {
				azure.AzuKeySize = azuKeySize
			}

			kmsConfig.Azure = &azure
		case HashicorpVaultEARType:
			hashicorp := HcVaultAuthConfigField{}
			if vaultToken, ok := credentials[HashicorpVaultTokenField].(string); ok {
				hashicorp.HcVaultToken = vaultToken
			}
			if vaultAddress, ok := credentials[HashicorpVaultAddressField].(string); ok {
				hashicorp.HcVaultAddress = vaultAddress
			}
			if vaultEngine, ok := credentials[HashicorpVaultEngineField].(string); ok {
				hashicorp.HcVaultEngine = vaultEngine
			}
			if vaultMountPath, ok := credentials[HashicorpVaultMountPathField].(string); ok {
				hashicorp.HcVaultMountPath = vaultMountPath
			}
			if vaultKeyName, ok := credentials[HashicorpVaultKeyNameField].(string); ok {
				hashicorp.HcVaultKeyName = vaultKeyName
			}
			if vaultRoleID, ok := credentials[HashicorpVaultRoleIDField].(string); ok {
				hashicorp.HcVaultRoleID = vaultRoleID
			}
			if vaultSecretID, ok := credentials[HashicorpVaultSecretIDField].(string); ok {
				hashicorp.HcVaultSecretID = vaultSecretID
			}
			if vaultAuthNamespace, ok := credentials[HashicorpVaultAuthNamespaceField].(string); ok {
				hashicorp.HcVaultAuthNamespace = vaultAuthNamespace
			}
			if vaultPkiRole, ok := credentials["HC_VAULT_PKI_ROLE"].(string); ok {
				hashicorp.HcVaultPkiRole = vaultPkiRole
			}
			if vaultTTL, ok := credentials["HC_VAULT_TTL"].(int64); ok {
				hashicorp.HcVaultTTL = vaultTTL
			}
			if vaultTTLExpiry, ok := credentials["HC_VAULT_TTL_EXPIRY"].(int64); ok {
				hashicorp.HcVaultTTLExpiry = vaultTTLExpiry
			}

			kmsConfig.Hashicorp = &hashicorp

		}
	}

	return kmsConfig, nil
}

// AzureAlgorithm type
type AzureAlgorithm struct {
	KeyType        string
	KeySize        []int
	DefaultKeySize int
}

var azuAlgorithmKeySize = []AzureAlgorithm{
	{"RSA", []int{2048, 3072, 4096}, 2048},
}

// AzureCheckKeySizeForAlgo check key size for algorithm
func AzureCheckKeySizeForAlgo(keySize int, algo string) bool {
	for _, v := range azuAlgorithmKeySize {
		if strings.Compare(v.KeyType, algo) == 0 {
			if slices.Contains(v.KeySize, keySize) {
				return true
			}
		}
	}
	return false
}

// AzureGetDefaultKeySizeForAlgo get key size for algorithm
func AzureGetDefaultKeySizeForAlgo(algo string) int {
	for _, v := range azuAlgorithmKeySize {
		if strings.Compare(v.KeyType, algo) == 0 {
			return v.DefaultKeySize
		}
	}
	return 0
}
