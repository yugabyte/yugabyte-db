/*
* Copyright (c) YugaByte, Inc.
 */

package template

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strconv"

	// "path/filepath"

	"strings"
	"text/template"

	"github.com/spf13/viper"
	"sigs.k8s.io/yaml"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// GetYamlPathData reads the key text from the input file and returns it as a string.
func GetYamlPathData(text string) string {
	// TODO: we should validate if we ever send a key that has spaces.
	pathString := strings.ReplaceAll(text, " ", "")
	log.DebugLF("Reading from viper: " + pathString)
	log.DebugLF(fmt.Sprintf("Reading from viper: %s", viper.Get(pathString)))
	return viper.GetString(pathString)
}

func ToYaml(path string) string {
	// Convert the path to a YAML format
	// This is a simple conversion, you might want to adjust it based on your needs
	pathString := strings.ReplaceAll(path, " ", "")
	log.DebugLF("Reading from viper: " + pathString)
	log.DebugLF(fmt.Sprintf("Reading from viper: %s", viper.Get(pathString)))
	d := viper.Get(pathString)
	outB, err := yaml.Marshal(d)
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	o := strings.TrimSuffix(string(outB), "\n")
	log.DebugLF("YAML output:\n" + o)
	return o
}

func indent(spaces int, s string) string {
	// Indent the given string with the specified number of spaces
	indentation := strings.Repeat(" ", spaces)
	lines := strings.Split(s, "\n")
	for i, line := range lines {
		lines[i] = indentation + line
	}
	return strings.Join(lines, "\n")
}

func nindent(spaces int, s string) string {
	i := indent(spaces, s)
	return "\n" + i
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
func readConfigAndTemplate(configYmlFileName string, service components.Service) ([]byte, error) {

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
		"toYaml":            ToYaml,
		"indent":            indent,
		"nindent":           nindent,
		"logrotateD":        func() string { return filepath.Join(common.GetSoftwareRoot(), "logrotate", "logrotate.d") },
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

	log.DebugLF(string(createdBytes))
	jsonString, jsonStringErr := yaml.YAMLToJSON(createdBytes)
	if jsonStringErr != nil {
		log.DebugLF("Error converting YAML to JSON: " + string(jsonString))
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
func GenerateTemplate(component components.Service) error {
	return GenerateTemplateWithPredicate(component, func(string) bool { return true })
}

// GenerateTemplateWithPredicate allows filtering out services by name.
func GenerateTemplateWithPredicate(component components.Service, servicePredicate func(string) bool) error {
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
		if !servicePredicate(serviceName) {
			continue
		}
		serviceFileName := fmt.Sprint(service.(map[string]interface{})["fileName"])
		serviceContents := fmt.Sprint(service.(map[string]interface{})["contents"])
		fileMode := fs.FileMode(0)
		if modeStr, ok := service.(map[string]interface{})["fileMode"]; ok {
			modeOctal, err := strconv.ParseUint(fmt.Sprint(modeStr), 8, 32)
			if err != nil {
				return err
			}
			fileMode = fs.FileMode(modeOctal)
		}

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
			additionalEntryString := strings.TrimSuffix(GetYamlPathData("platform.additional"), "\n")

			log.DebugLF("Writing addition data to yb-platform config: " + additionalEntryString)
			if _, err := file.WriteString(additionalEntryString); err != nil {
				return err
			}
		}

		if int(fileMode) != 0 {
			if err := os.Chmod(serviceFileName, fileMode); err != nil {
				return err
			}
		}
		log.Debug("Templated configuration for " + serviceName +
			" succesfully applied.")
	}
	return nil
}
