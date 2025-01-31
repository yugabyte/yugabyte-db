/*
* Copyright (c) YugaByte, Inc.
 */

package config

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"

	// "path/filepath"

	"strings"
	"text/template"

	"github.com/spf13/viper"
	"sigs.k8s.io/yaml"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// GetYamlPathData reads the key text from the input file and returns it as a string.
func GetYamlPathData(text string) string {
	// TODO: we should validate if we ever send a key that has spaces.
	pathString := strings.ReplaceAll(text, " ", "")
	return viper.GetString(pathString)
}

// GetYamlPathSliceData reads the key text from the input file and returns it as a slice of string
func GetYamlPathSliceData(text string) []string {
	pathString := strings.ReplaceAll(text, " ", "")
	// Use viper to directly retrieve the slice
	list := viper.GetStringSlice(pathString)

	// Format the slice
	var formattedList []string
	for _, item := range list {
		formattedList = append(formattedList, fmt.Sprintf("%q", item))
	}
	return formattedList
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
		"yamlPathSlice":     GetYamlPathSliceData,
		"installRoot":       common.GetSoftwareRoot,
		"installVersionDir": common.GetInstallerSoftwareDir,
		"baseInstall":       common.GetBaseInstall,
		"splitInput":        common.SplitInput,
		"removeQuotes":      common.RemoveQuotes,
		"systemdLogMethod":  common.SystemdLogMethod,
	}

	tmpl, err := template.New(configYmlFileName).
		Funcs(funcMap).ParseFiles(fmt.Sprintf("%s/%s", common.GetTemplatesDir(), configYmlFileName))

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
func GenerateTemplate(component common.Component) error {
	log.Debug("Generating config files for " + component.Name())
	createdBytes, err := readConfigAndTemplate(component.TemplateFile(), component)
	if err != nil {
		return err
	}

	jsonData, err := readYAMLtoJSON(createdBytes)
	if err != nil {
		return err
	}

	numberOfServices := len(jsonData["services"].([]interface{}))

	for i := 0; i < numberOfServices; i++ {
		service := jsonData["services"].([]interface{})[i]
		serviceName := fmt.Sprint(service.(map[string]interface{})["name"])
		serviceFileName := fmt.Sprint(service.(map[string]interface{})["fileName"])
		serviceContents := fmt.Sprint(service.(map[string]interface{})["contents"])

		// Only write the service files to the appropriate file location if we are
		// running as root (since we might not be able to write to /etc/systemd)
		// in the non-root case.
		log.Debug("Creating file from template: " + serviceFileName)
		if _, err := WriteBytes([]byte(serviceContents), []byte(serviceFileName)); err != nil {
			return err
		}

		// TODO: Use viper to parse additional entries
		if component.Name() == "yb-platform" && serviceName == "platformConfig" {
			file, err := os.OpenFile(serviceFileName, os.O_APPEND|os.O_WRONLY, 0644)
			if err != nil {
				return err
			}
			defer file.Close()

			// Add the additional raw text to yb-platform.conf if it exists.
			additionalEntryString := strings.TrimSuffix(GetYamlPathData(".platform.additional"), "\n")

			log.DebugLF("Writing addition data to yb-platform config: " + additionalEntryString)
			if _, err := file.WriteString(additionalEntryString); err != nil {
				return err
			}
		}
		log.Debug("Templated configuration for " + serviceName +
			" succesfully applied.")
	}
	return nil
}
