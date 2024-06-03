/*
 * Copyright (c) YugaByte, Inc.
 */

package util

import (
	"encoding/json"
	"fmt"
	"os"
	"reflect"
	"strings"

	awsCreds "github.com/aws/aws-sdk-go/aws/credentials"
)

// GCPCredentials is a struct to hold values retrieved by parsing the GCE credentials json file
type GCPCredentials struct {
	AuthProviderX509CertURL string `json:"auth_provider_x509_cert_url,omitempty"`
	AuthURI                 string `json:"auth_uri,omitempty"`
	ClientEmail             string `json:"client_email"`
	ClientID                string `json:"client_id"`
	ClientX509CertURL       string `json:"client_x509_cert_url"`
	PrivateKey              string `json:"private_key"`
	PrivateKeyID            string `json:"private_key_id"`
	ProjectID               string `json:"project_id"`
	TokenURI                string `json:"token_uri,omitempty"`
	Type                    string `json:"type"`
}

// AzureCredentials required for cloud provider
type AzureCredentials struct {
	TenantID       string `json:"tenant_id"`
	SubscriptionID string `json:"subscription_id"`
	ClientSecret   string `json:"client_secret"`
	ClientID       string `json:"client_id"`
	ResourceGroup  string `json:"resource_group"`
}

// KuberenetesMetadata to extract image pull secret name
type KuberenetesMetadata struct {
	Name string `yaml:"name"`
}

// KuberenetesPullSecretYAML to extract image pull secret name
type KubernetesPullSecretYAML struct {
	Metadata *KuberenetesMetadata `yaml:"metadata"`
}

// gcpGetCredentials retrieves the GCE credentials from env variable and returns gcpCredentials
// struct
func gcpGetCredentials(filePath string) (GCPCredentials, error) {
	var gcsCredsByteArray []byte
	var err error
	if len(filePath) == 0 {
		gcsCredsByteArray, err = gcpCredentialsFromEnv()
		if err != nil {
			return GCPCredentials{}, err
		}
	} else {
		gcsCredsByteArray, err = gcpCredentialsFromFilePath(filePath)
	}
	gcsCredsJSON := GCPCredentials{}
	err = json.Unmarshal(gcsCredsByteArray, &gcsCredsJSON)
	if err != nil {
		return GCPCredentials{}, fmt.Errorf("Failed unmarshalling GCE credentials: %s", err)
	}
	return gcsCredsJSON, nil

}

// gcpCredentialsFromEnv retrieves credentials from "GOOGLE_APPLICATION_CREDENTIALS"
func gcpCredentialsFromEnv() ([]byte, error) {
	return gcpCredentialsFromFilePath(os.Getenv(GCPCredentialsEnv))
}

// gcpCredentialsFromFilePath retrieves credentials from any given file path
func gcpCredentialsFromFilePath(filePath string) ([]byte, error) {
	if filePath == "" {
		return nil, fmt.Errorf("%s env variable is empty", GCPCredentialsEnv)
	}
	gcsCredsByteArray, err := os.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("Failed reading data from %s: %s", filePath, err)
	}
	return gcsCredsByteArray, nil
}

// gcpGetJSONTag returns the JSON field name of the struct field
func gcpGetJSONTag(val reflect.StructField) string {
	switch jsonTag := val.Tag.Get("json"); jsonTag {
	case "-":
	case "":
		return val.Name
	default:
		parts := strings.Split(jsonTag, ",")
		name := parts[0]
		if name == "" {
			name = val.Name
		}
		return name
	}
	return ""
}

// GcpGetCredentialsAsString returns the GCE JSON file contents as a string
func GcpGetCredentialsAsString() (string, error) {
	gcsCredsJSON, err := gcpGetCredentials("")
	if err != nil {
		return "", err
	}
	v := reflect.ValueOf(gcsCredsJSON)
	var gcsCredString string
	gcsCredString = "{ "
	for i := 0; i < v.NumField(); i++ {
		var s string
		field := gcpGetJSONTag(v.Type().Field(i))

		if field == "private_key" {
			valString := strings.Replace(v.Field(i).Interface().(string), "\n", "\\n", -1)
			s = "\"" + field + "\"" + ": " + "\"" + valString + "\""

		} else {
			s = "\"" + field + "\"" + ": " + "\"" + v.Field(i).Interface().(string) + "\""
		}
		if gcsCredString[len(gcsCredString)-2] != '{' {
			gcsCredString = gcsCredString + " , " + s
		} else {
			gcsCredString = gcsCredString + s
		}
	}
	gcsCredString = gcsCredString + "}"
	return gcsCredString, nil
}

// GcpGetCredentialsAsMap returns the GCE JSON file contents as a map
func GcpGetCredentialsAsMap() (map[string]interface{}, error) {
	gcsCredsMap := make(map[string]interface{})
	gcsCredsJSON, err := gcpGetCredentials("")
	if err != nil {
		return nil, err
	}
	v := reflect.ValueOf(gcsCredsJSON)
	for i := 0; i < v.NumField(); i++ {
		tag := gcpGetJSONTag(v.Type().Field(i))
		gcsCredsMap[tag] = v.Field(i).Interface().(string)
	}
	return gcsCredsMap, nil
}

// GcpGetCredentialsAsStringFromFilePath returns the GCE JSON file contents as a string
func GcpGetCredentialsAsStringFromFilePath(filePath string) (string, error) {
	gcsCredsJSON, err := gcpGetCredentials(filePath)
	if err != nil {
		return "", err
	}
	v := reflect.ValueOf(gcsCredsJSON)
	var gcsCredString string
	gcsCredString = "{ "
	for i := 0; i < v.NumField(); i++ {
		var s string
		field := gcpGetJSONTag(v.Type().Field(i))

		if field == "private_key" {
			valString := strings.Replace(v.Field(i).Interface().(string), "\n", "\\n", -1)
			s = "\"" + field + "\"" + ": " + "\"" + valString + "\""

		} else {
			s = "\"" + field + "\"" + ": " + "\"" + v.Field(i).Interface().(string) + "\""
		}
		if gcsCredString[len(gcsCredString)-2] != '{' {
			gcsCredString = gcsCredString + " , " + s
		} else {
			gcsCredString = gcsCredString + s
		}
	}
	gcsCredString = gcsCredString + "}"
	return gcsCredString, nil
}

// GcpGetCredentialsAsMapFromFilePath returns the GCE JSON file
// contents as a map from the file path
func GcpGetCredentialsAsMapFromFilePath(filePath string) (map[string]interface{}, error) {
	gcsCredsMap := make(map[string]interface{})
	gcsCredsJSON, err := gcpGetCredentials(filePath)
	if err != nil {
		return nil, err
	}
	v := reflect.ValueOf(gcsCredsJSON)
	for i := 0; i < v.NumField(); i++ {
		tag := gcpGetJSONTag(v.Type().Field(i))
		gcsCredsMap[tag] = v.Field(i).Interface().(string)
	}
	return gcsCredsMap, nil
}

// AwsCredentialsFromEnv retrives values of "AWS_ACCESS_KEY_ID" and "AWS_SECRET_ACCESS_KEY" from
// env variables
func AwsCredentialsFromEnv() (awsCreds.Value, error) {

	awsCredentials, err := awsCreds.NewEnvCredentials().Get()
	if err != nil {
		return awsCreds.Value{}, fmt.Errorf("Error getting AWS env credentials %s", err)
	}
	return awsCredentials, nil
}

// AzureStorageCredentialsFromEnv retrives value of "AZURE_STORAGE_SAS_TOKEN" from env variables
func AzureStorageCredentialsFromEnv() (string, error) {
	azureSasToken, isPresent := os.LookupEnv(AzureStorageSasTokenEnv)
	if !isPresent {
		return "", fmt.Errorf("%s env variable not found", AzureStorageSasTokenEnv)
	}
	return azureSasToken, nil
}

// AzureCredentialsFromEnv retrives azure credentials from env variables
func AzureCredentialsFromEnv() (AzureCredentials, error) {

	// get client id, client secret, tenat id, resource group and subscription id
	var azureCreds AzureCredentials
	var isPresentClientID, isPresentClientSecret, isPresentSubscriptionID bool
	var isPresentTenantID, isPresentRG bool
	errorString := "Empty env variable: "
	azureCreds.ClientID, isPresentClientID = os.LookupEnv(AzureClientIDEnv)
	if !isPresentClientID {
		errorString = fmt.Sprintf("%s%s ", errorString, AzureClientIDEnv)
	}
	azureCreds.ClientSecret, isPresentClientSecret = os.LookupEnv(AzureClientSecretEnv)
	if !isPresentClientSecret {
		errorString = fmt.Sprintf("%s%s ", errorString, AzureClientSecretEnv)
	}
	azureCreds.SubscriptionID, isPresentSubscriptionID = os.LookupEnv(AzureSubscriptionIDEnv)
	if !isPresentSubscriptionID {
		errorString = fmt.Sprintf("%s%s ", errorString, AzureSubscriptionIDEnv)
	}
	azureCreds.TenantID, isPresentTenantID = os.LookupEnv(AzureTenantIDEnv)
	if !isPresentTenantID {
		errorString = fmt.Sprintf("%s%s ", errorString, AzureTenantIDEnv)
	}
	azureCreds.ResourceGroup, isPresentRG = os.LookupEnv(AzureRGEnv)
	if !isPresentRG {
		errorString = fmt.Sprintf("%s%s ", errorString, AzureRGEnv)
	}
	if !(isPresentClientID && isPresentClientSecret && isPresentRG &&
		isPresentSubscriptionID && isPresentTenantID) {
		return AzureCredentials{}, fmt.Errorf(errorString)
	}
	return azureCreds, nil
}

// ReadSSHPrivateKey retrives private key file contents from env variable
func ReadSSHPrivateKey(filePath string) (*string, error) {
	fileContentByte, err := os.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("Failed reading data from %s: %s", filePath, err)
	}
	fileContent := string(fileContentByte)
	return &fileContent, nil
}
