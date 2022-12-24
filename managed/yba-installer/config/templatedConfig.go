/*
* Copyright (c) YugaByte, Inc.
 */

package config

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"

	// "path/filepath"
	"crypto/rand"
	"strings"
	"text/template"

	"github.com/spf13/viper"
	"github.com/xeipuuv/gojsonschema"
	"sigs.k8s.io/yaml"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// PlatformAppSecret is special cased because it is not configurable by the user.
var platformAppSecret string = generateRandomStringURLSafe(64)

// RandomDbPassword is applied to the templated configuration file if not set.
var randomDbPassword string = generateRandomStringURLSafe(32)

// Password to protect the keystore
var randomKeystorePassword string = generateRandomStringURLSafe(32)

// ValidateJSONSchema checks that the parameters in each component's config file are indeed
// valid by turning the input YAML file into a JSON file, and then validating that
// the parameters have been specified appropriately using the available
// JSON schema.
func validateJSONSchema() {

	createdBytes, err := os.ReadFile(common.InputFile)
	if err != nil {
		log.Fatal(fmt.Sprintf("Error: %v.", err))
	}

	jsonString, jsonStringErr := yaml.YAMLToJSON(createdBytes)
	if jsonStringErr != nil {
		log.Fatal(fmt.Sprintf("Error: %v.\n", jsonStringErr))
	}

	var jsonData map[string]interface{}
	if jsonDataError := json.Unmarshal([]byte(jsonString), &jsonData); jsonDataError != nil {
		log.Fatal(fmt.Sprintf("Error: %v.\n", jsonDataError))
	}

	jsonBytesInput, _ := json.Marshal(jsonData)

	jsonStringInput := string(jsonBytesInput)

	jsonSchemaName := fmt.Sprintf("file://./%s/yba-installer-input-json-schema.json", common.ConfigDir)

	schemaLoader := gojsonschema.NewReferenceLoader(jsonSchemaName)
	documentLoader := gojsonschema.NewStringLoader(jsonStringInput)

	result, err := gojsonschema.Validate(schemaLoader, documentLoader)

	// Panic to automatically exit the Templating Phase if the passed-in parameters are
	// not valid.
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	if result.Valid() {
		log.Debug("The YBA Installer configuration is valid.\n")
	} else {
		log.Info("The YBA Installer configuration is not valid! See Errors: \n")
		for _, desc := range result.Errors() {
			log.Fatal(fmt.Sprintf("- %s\n", desc))
		}
	}

}

// GetYamlPathData reads the key text from the input file and returns it as a string.
// Also does some custom processing for passwords by returning random defaults.
func GetYamlPathData(text string) string {
	// TODO: we should validate if we ever send a key that has spaces.
	pathString := strings.ReplaceAll(text, " ", "")

	// Handle default values that are not set in the input config.
	if strings.Contains(pathString, "appSecret") {
		return platformAppSecret
	}

	val := viper.GetString(pathString)
	if strings.Contains(pathString, "platformDbPassword") && val == "" {
		return randomDbPassword
	}

	if strings.Contains(pathString, "keyStorePassword") && val == "" {
		return randomKeystorePassword
	}

	// TODO: This is for non root. Root should always be yugabyte user (defaulted in input.yml)
	if !common.HasSudoAccess() {
		if strings.Contains(pathString, "platformDbUser") {
			return strings.ReplaceAll(strings.TrimSuffix(common.GetCurrentUser(), "\n"), " ", "")
		}
	}
	return val

}

// ReadConfigAndTemplate Reads info from input config file and sets
// all template parameters for each individual config file directly, without
// having to rely on variable names in app data.
func readConfigAndTemplate(configYmlFileName string, service common.Component) ([]byte, error) {

	// First we create a FuncMap with which to register the function.
	funcMap := template.FuncMap{

		// The name "yamlPath" is what the function will be called
		// in the template text.
		"yamlPath":          GetYamlPathData,
		"installRoot":       common.GetInstallRoot,
		"installVersionDir": common.GetInstallVersionDir,
		"baseInstall":			 common.GetBaseInstall,
	}

	tmpl, err := template.New(configYmlFileName).
		Funcs(funcMap).ParseFiles(fmt.Sprintf("%s/%s", common.ConfigDir, configYmlFileName))

	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
		return nil, err
	}

	var buf bytes.Buffer
	if err := tmpl.Execute(&buf, service); err != nil {
		log.Fatal("Error: " + err.Error() + ".")
		return nil, err
	}

	return buf.Bytes(), nil

}

func readYAMLtoJSON(createdBytes []byte) (map[string]interface{}, error) {

	jsonString, jsonStringErr := yaml.YAMLToJSON(createdBytes)
	if jsonStringErr != nil {
		log.Fatal(fmt.Sprintf("Error: %v.\n", jsonStringErr))
		return nil, jsonStringErr
	}

	var jsonData map[string]interface{}
	if jsonDataError := json.Unmarshal([]byte(jsonString), &jsonData); jsonDataError != nil {
		log.Fatal(fmt.Sprintf("Error: %v.\n", jsonDataError))
		return nil, jsonDataError
	}

	return jsonData, nil

}

// WriteBytes writes the byteSlice data to the specified fileName path.
func WriteBytes(byteSlice []byte, fileName []byte) ([]byte, error) {

	fileNameString := string(fileName)
	log.Info("Creating file (and directory path): " + fileNameString)
	file, createErr := common.Create(fileNameString)

	if createErr != nil {
		log.Fatal("Error: " + createErr.Error() + ".")
		return nil, createErr
	}

	defer file.Close()
	_, writeErr := file.Write(byteSlice)
	if writeErr != nil {
		log.Fatal("Error: " + writeErr.Error() + ".")
		return nil, writeErr
	}

	return []byte("Wrote bytes to " + fileNameString + " successfully!"), nil

}

// GenerateTemplate of a particular component.
func GenerateTemplate(component common.Component) {

	validateJSONSchema()

	createdBytes, _ := readConfigAndTemplate(component.TemplateFile(), component)

	jsonData, _ := readYAMLtoJSON(createdBytes)

	numberOfServices := len(jsonData["services"].([]interface{}))

	for i := 0; i < numberOfServices; i++ {

		service := jsonData["services"].([]interface{})[i]
		serviceName := fmt.Sprint(service.(map[string]interface{})["name"])

		serviceFileName := fmt.Sprint(service.(map[string]interface{})["fileName"])

		serviceContents := fmt.Sprint(service.(map[string]interface{})["contents"])

		// Only write the service files to the appropriate file location if we are
		// running as root (since we might not be able to write to /etc/systemd)
		// in the non-root case.

		if !common.HasSudoAccess() {

			if !strings.Contains(serviceName, "Service") {

				WriteBytes([]byte(serviceContents), []byte(serviceFileName))

			}

		} else {

			WriteBytes([]byte(serviceContents), []byte(serviceFileName))

		}

		// TODO: Use viper to parse additional entries
		if component.Name() == "yb-platform" {

			file, err := os.OpenFile(serviceFileName, os.O_APPEND|os.O_WRONLY, 0644)
			if err != nil {
				log.Fatal("Error: " + err.Error() + ".")
			}
			defer file.Close()

			// Add the additional raw text to yb-platform.conf if it exists.
			additionalEntryString := strings.TrimSuffix(GetYamlPathData(".additional"), "\n")

			if _, err := file.WriteString(additionalEntryString); err != nil {
				log.Fatal("Error: " + err.Error() + ".")
			}

		}

		log.Debug("Templated configuration for " + serviceName +
			" succesfully applied.")

	}
}

func generateRandomBytes(n int) ([]byte, error) {

	b := make([]byte, n)
	_, err := rand.Read(b)
	if err != nil {
		return nil, err
	}
	return b, nil
}

// generateRandomStringURLSafe is used to generate random passwords.
func generateRandomStringURLSafe(n int) string {

	b, _ := generateRandomBytes(n)
	return base64.URLEncoding.EncodeToString(b)
}
