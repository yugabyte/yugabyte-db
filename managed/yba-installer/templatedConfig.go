/*
* Copyright (c) YugaByte, Inc.
 */

package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	yaml2 "github.com/goccy/go-yaml"
	"github.com/xeipuuv/gojsonschema"
	"io/ioutil"
	"os"
	"path/filepath"
	"sigs.k8s.io/yaml"
	"strings"
	"text/template"
)

//PlatformAppSecret is special cased because it is not configurable by the user.
var platformAppSecret string = GenerateRandomStringURLSafe(64)

//CorsOrigin is special cased because it is not configurable by the user.
var corsOrigin string = GenerateCORSOrigin()

// RandomDbPassword is applied to the templated configuration file if not already
// set in the configuration file (arbitrary length 20).
var randomDbPassword string = GenerateRandomStringURLSafe(20)

// ValidateJSONSchema checks that the parameters in each component's config file are indeed
// valid by turning the input YAML file into a JSON file, and then validating that
// the parameters have been specified appropriately using the available
// JSON schema.
func validateJSONSchema(filename string) {

	createdBytes, err := ioutil.ReadFile(filename)
	if err != nil {
		LogError(fmt.Sprintf("Error: %v.", err))
	}

	jsonString, jsonStringErr := yaml.YAMLToJSON(createdBytes)
	if jsonStringErr != nil {
		LogError(fmt.Sprintf("Error: %v.\n", jsonStringErr))
	}

	var jsonData map[string]interface{}
	if jsonDataError := json.Unmarshal([]byte(jsonString), &jsonData); jsonDataError != nil {
		LogError(fmt.Sprintf("Error: %v.\n", jsonDataError))
	}

	jsonBytesInput, _ := json.Marshal(jsonData)

	jsonStringInput := string(jsonBytesInput)

	jsonSchemaName := "file://./configFiles/yba-installer-input-json-schema.json"

	schemaLoader := gojsonschema.NewReferenceLoader(jsonSchemaName)
	documentLoader := gojsonschema.NewStringLoader(jsonStringInput)

	result, err := gojsonschema.Validate(schemaLoader, documentLoader)

	// Panic to automatically exit the Templating Phase if the passed-in parameters are
	// not valid.
	if err != nil {
		LogError("Error: " + err.Error() + ".")
	}

	if result.Valid() {
		LogDebug("The YBA Installer configuration is valid.\n")
	} else {
		LogInfo("The YBA Installer configuration is not valid! See Errors: \n")
		for _, desc := range result.Errors() {
			LogError(fmt.Sprintf("- %s\n", desc))
		}
	}

}

// Custom function to return Yaml data that we call from within the templated
// configuration file, to better support future file generation.
func getYamlPathData(text string) string {

	inputYml, errYml := ioutil.ReadFile("yba-installer-input.yml")
	if errYml != nil {
		LogError(fmt.Sprintf("Error: %v.", errYml))
	}

	pathString := strings.ReplaceAll(text, " ", "")

	if strings.Contains(pathString, "appSecret") {
		return platformAppSecret
	} else if strings.Contains(pathString, "corsOrigin") {
		return corsOrigin
	} else {
		yamlPathString := "$" + pathString
		path, err := yaml2.PathString(yamlPathString)
		if err != nil {
			LogError("Yaml Path string " + yamlPathString + " not valid.")
		}

		var val string
		err = path.Read(bytes.NewReader(inputYml), &val)
		if strings.Contains(pathString, "platformDbPassword") && val == "" {
			return randomDbPassword
		}
		// To keep the password constant during reconfiguration.
		if strings.Contains(pathString, "keyStorePassword") && val == "" {
			return "password"
		}
		// Have to be regular user if not root, since we will not have access to the
		// postgres user.
		if !hasSudoAccess() {
			if strings.Contains(pathString, "platformDbUser") {
				currentUser, _ := ExecuteBashCommand("bash", []string{"-c", "whoami"})
				currentUser = strings.ReplaceAll(strings.TrimSuffix(currentUser, "\n"), " ", "")
				return currentUser
			}
		}
		return val
	}
}

// Custom function to set Platform Environment Variables, in the case where we
// are executing as Non-root and not launching Platform as a systemd service.
func setYamlEnvironmentVariables(text string) string {

	inputYml, errYml := ioutil.ReadFile("yba-installer-input.yml")
	if errYml != nil {
		LogError(fmt.Sprintf("Error: %v.", errYml))
	}

	envVariableString := strings.ReplaceAll(text, " ", "")

	envValueString := strings.ReplaceAll(envVariableString, ".environmentVariables", "")

	if strings.Contains(envValueString, "appSecret") {

		os.Setenv("PLATFORM_APP_SECRET", platformAppSecret)

		return "PLATFORM_APP_SECRET"

	} else if strings.Contains(envValueString, "corsOrigin") {

		os.Setenv("CORS_ORIGIN", corsOrigin)

		return "CORS_ORIGIN"

	} else if strings.Contains(envValueString, "metricsUrl") {

		port := getYamlPathData(".prometheus.externalPort")

		os.Setenv("METRICS_URL", "http://127.0.0.1:"+port+"/api/v1")

		return "METRICS_URL"

	} else if strings.Contains(envValueString, "metricsManagementUrl") {

		port := getYamlPathData(".prometheus.externalPort")

		os.Setenv("METRICS_MANAGEMENT_URL", "http://127.0.0.1:"+port+"/-")

		return "METRICS_MANAGEMENT_URL"

	} else {
		yamlEnvVariableString := "$" + envVariableString
		yamlEnvValueString := "$" + envValueString

		envVariablePath, errVar := yaml2.PathString(yamlEnvVariableString)
		if errVar != nil {
			LogError("Yaml Env Variable string " + yamlEnvVariableString + " not valid.")
		}

		envValuePath, errValue := yaml2.PathString(yamlEnvValueString)
		if errValue != nil {
			LogError("Yaml Env Value string " + yamlEnvValueString + " not valid.")
		}

		var envVariable string
		var envValue string

		errVar = envVariablePath.Read(bytes.NewReader(inputYml), &envVariable)

		if errVar != nil {
			LogError("Unable to read " + envVariableString + "in the config file.")
		}

		errValue = envValuePath.Read(bytes.NewReader(inputYml), &envValue)

		if errValue != nil {
			LogError("Unable to read " + envValueString + "in the config file.")
		}

		if strings.Contains(envValueString, "platformDbPassword") && envValue == "" {

			os.Setenv("PLATFORM_DB_PASSWORD", randomDbPassword)
			return "PLATFORM_DB_PASSWORD"

		} else if strings.Contains(envValueString, "platformDbUser") {

			os.Setenv("DEVOPS_HOME", INSTALL_ROOT+"/devops")
			os.Setenv("SWAMPER_TARGET_PATH", INSTALL_ROOT+"/swamper_targets")
			os.Setenv("SWAMPER_RULES_PATH", INSTALL_ROOT+"/swamper_rules")

			if hasSudoAccess() {

				os.Setenv("PLATFORM_DB_USER", "postgres")
				return "PLATFORM_DB_USER"

			} else {

				currentUser := GetCurrentUser()
				os.Setenv("PLATFORM_DB_USER", currentUser)

				return "PLATFORM_DB_USER"

			}

		} else {
			os.Setenv(envVariable, envValue)
			return envVariable

		}

	}
}

func getOStype() string {

	if containsSubstring(yumList, DetectOS()) && hasSudoAccess() {

		return "yum"

	} else {

		return "apt"
	}

}

// ReadConfigAndTemplate Reads info from input config file and sets
// all template parameters for each individual config file directly, without
// having to rely on variable names in app data.
func readConfigAndTemplate(configYmlFileName string) ([]byte, error) {

	// First we create a FuncMap with which to register the function.
	funcMap := template.FuncMap{

		// The name "yamlPath" is what the function will be called
		// in the template text.
		"yamlPath":          getYamlPathData,
		"yamlSetEnv":        setYamlEnvironmentVariables,
		"installRoot":       GetInstallRoot,
		"installVersionDir": GetInstallVersionDir,
		"osType":            getOStype,
	}

	tmpl, err := template.New(filepath.Base("configFiles/" + configYmlFileName)).
		Funcs(funcMap).ParseFiles("configFiles/" + configYmlFileName)

	if err != nil {
		LogError("Error: " + err.Error() + ".")
		return nil, err
	}

	var buf bytes.Buffer
	if err := tmpl.Execute(&buf, ""); err != nil {
		LogError("Error: " + err.Error() + ".")
		return nil, err
	}

	return buf.Bytes(), nil

}

func readYAMLtoJSON(createdBytes []byte) (map[string]interface{}, error) {

	jsonString, jsonStringErr := yaml.YAMLToJSON(createdBytes)
	if jsonStringErr != nil {
		LogError(fmt.Sprintf("Error: %v.\n", jsonStringErr))
		return nil, jsonStringErr
	}

	var jsonData map[string]interface{}
	if jsonDataError := json.Unmarshal([]byte(jsonString), &jsonData); jsonDataError != nil {
		LogError(fmt.Sprintf("Error: %v.\n", jsonDataError))
		return nil, jsonDataError
	}

	return jsonData, nil

}

// WriteBytes writes the byteSlice data to the specified fileName path.
func WriteBytes(byteSlice []byte, fileName []byte) ([]byte, error) {

	fileNameString := string(fileName)

	file, createErr := Create(fileNameString)

	if createErr != nil {
		LogError("Error: " + createErr.Error() + ".")
		return nil, createErr
	}

	defer file.Close()
	_, writeErr := file.Write(byteSlice)
	if writeErr != nil {
		LogError("Error: " + writeErr.Error() + ".")
		return nil, writeErr
	}

	return []byte("Wrote bytes to " + fileNameString + " successfully!"), nil

}

//GenerateTemplatedConfiguration creates the templated configuration files for
//all Yugabyte Anywhere services.
func GenerateTemplatedConfiguration(services ...string) {

	inputYmlName := "yba-installer-input.yml"

	validateJSONSchema(inputYmlName)

	outputYmlList := []string{"yba-installer-prometheus.yml",
		"yba-installer-platform.yml", "yba-installer-postgres.yml"}

	if len(services) != 0 {
		outputYmlList = services
	}

	for _, outYmlName := range outputYmlList {

		createdBytes, _ := readConfigAndTemplate(outYmlName)

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

			if !hasSudoAccess() {

				if !strings.Contains(serviceName, "Service") {

					WriteBytes([]byte(serviceContents), []byte(serviceFileName))

				}

			} else {

				WriteBytes([]byte(serviceContents), []byte(serviceFileName))

			}

			if strings.Contains(serviceFileName, "yb-platform.conf") {

				file, err := os.OpenFile(serviceFileName, os.O_APPEND|os.O_WRONLY, 0644)
				if err != nil {
					LogError("Error: " + err.Error() + ".")
				}
				defer file.Close()

				// Add the additional raw text to yb-platform.conf if it exists.
				additionalEntryString := strings.TrimSuffix(getYamlPathData(".additional"), "\n")

				if _, err := file.WriteString(additionalEntryString); err != nil {
					LogError("Error: " + err.Error() + ".")
				}

			}

			LogDebug("Templated configuration for " + serviceName +
				" succesfully applied.")

		}
	}
}
