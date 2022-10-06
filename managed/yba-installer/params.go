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
	yaml "sigs.k8s.io/yaml"
	"strconv"
	"strings"
)

func Params(key string, value string) {

	inputYmlBytes, errYml := ioutil.ReadFile("yba-installer-input.yml")
	if errYml != nil {
		LogError(fmt.Sprintf("Error: %v.", errYml))
	}

	// Check if the field exists via yamlPath, to confirm that it is configurable
	// by the user.
	pathString := strings.ReplaceAll(key, " ", "")
	yamlPathString := "$." + pathString
	path, err := yaml2.PathString(yamlPathString)
	var val string
	err = path.Read(bytes.NewReader(inputYmlBytes), &val)
	if err != nil {
		//Retain original CronJob in case parameter update is invalid.
		if !hasSudoAccess() {
			prometheus.CreateCronJob()
			postgres.CreateCronJob()
			platform.CreateCronJob()
		}
		LogError("Parameter " + key + " not configurable.")
	}

	// Have restructured configuration file so that we no longer have old/new
	// separation. Much easier for users to read.
	service := strings.Split(key, ".")[0]
	param := strings.Split(key, ".")[1]

	data, _ := ioutil.ReadFile("yba-installer-input.yml")
	var inputYml map[string]interface{}

	err = yaml.Unmarshal([]byte(data), &inputYml)
	if err != nil {
		if !hasSudoAccess() {
			//Retain original CronJob in case parameter update is invalid.
			prometheus.CreateCronJob()
			postgres.CreateCronJob()
			platform.CreateCronJob()
		}
		LogError(fmt.Sprintf("Error: %v.", err))
	}

	// Input the user's configuration setting into the configuration file, and verify that
	// they have specified a valid configuration setting using JSON schema validation. Only then
	// update the YAML if it safe to do so.

	// Convert to an int if the passed-in value is an integer, and
	// a bool if the passed-in value is a boolean.
	if _, err := strconv.Atoi(value); err == nil {
		valueInt, _ := strconv.Atoi(value)
		inputYml[service].(map[string]interface{})[param] = valueInt
	} else if _, err := strconv.ParseBool(value); err == nil {
		valueBool, _ := strconv.ParseBool(value)
		inputYml[service].(map[string]interface{})[param] = valueBool
	} else {
		inputYml[service].(map[string]interface{})[param] = value
	}

	updatedBytes, _ := yaml.Marshal(&inputYml)

	jsonString, jsonStringErr := yaml.YAMLToJSON(updatedBytes)
	if jsonStringErr != nil {
		//Retain original CronJob in case parameter update is invalid.
		prometheus.CreateCronJob()
		postgres.CreateCronJob()
		platform.CreateCronJob()
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

	if err != nil {
		//Retain original CronJob in case parameter update is invalid.
		prometheus.CreateCronJob()
		postgres.CreateCronJob()
		platform.CreateCronJob()
		LogError("Error: " + err.Error() + ".")
	}

	if result.Valid() {
		LogDebug("Your configuration setting is valid.\n")
		WriteBytes(updatedBytes, []byte("yba-installer-input.yml"))
	} else {
		LogInfo("Your configuration setting is not valid! See the below errors:\n")
		//Retain original CronJob in case parameter update is invalid.
		prometheus.CreateCronJob()
		postgres.CreateCronJob()
		platform.CreateCronJob()
		for _, desc := range result.Errors() {
			LogError(fmt.Sprintf("- %s\n", desc))
		}
	}

}
