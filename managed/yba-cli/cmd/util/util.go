/*
 * Copyright (c) YugaByte, Inc.
 */

package util

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"reflect"
	"regexp"
	"strconv"
	"strings"

	"github.com/AlecAivazis/survey/v2"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"gopkg.in/yaml.v2"
)

// StringSlice accepts array of interface and returns a pointer to slice of string
func StringSlice(in []interface{}) *[]string {
	var out []string
	for _, v := range in {
		out = append(out, v.(string))
	}
	return &out
}

// StringSliceFromString accepts slice of string and returns a pointer to slice of string
func StringSliceFromString(in []string) *[]string {
	if len(in) == 0 {
		return nil
	}
	return &in
}

// StringMap accepts a string -> interface map and returns pointer to string -> string map
func StringMap(in map[string]interface{}) *map[string]string {
	out := make(map[string]string)
	for k, v := range in {
		out[k] = v.(string)
	}
	return &out
}

// StringtoStringMap accepts a string -> string map and returns pointer to string -> string map
func StringtoStringMap(in map[string]string) *map[string]string {
	if len(in) == 0 {
		return nil
	}
	return &in
}

// MapFromSingletonList returns a map of string -> interface from a slice of interface
func MapFromSingletonList(in []interface{}) map[string]interface{} {
	if len(in) == 0 {
		return make(map[string]interface{})
	}
	return in[0].(map[string]interface{})
}

// MapListFromInterfaceList returns a map of string -> interface from a slice of interface
func MapListFromInterfaceList(in []interface{}) []map[string]interface{} {
	res := make([]map[string]interface{}, 0)
	if len(in) == 0 {
		return res
	}
	for _, i := range in {
		res = append(res, i.(map[string]interface{}))
	}
	return res
}

// GetBoolPointer returns a pointer to bool value
func GetBoolPointer(in bool) *bool {
	return &in
}

// GetStringPointer returns a pointer to string value
func GetStringPointer(in string) *string {
	if in == "" {
		return nil
	}
	return &in
}

// GetInt32Pointer returns a pointer to int32 value
func GetInt32Pointer(in int32) *int32 {
	if in == 0 {
		return nil
	}
	return &in
}

// GetInt64Pointer returns a pointer to int64 value
func GetInt64Pointer(in int64) *int64 {
	if in == 0 {
		return nil
	}
	return &in
}

// GetFloat64Pointer returns a pointer to float64 type
func GetFloat64Pointer(in float64) *float64 {
	if in == 0 {
		return nil
	}
	return &in
}

// CreateSingletonList returns a list of single entry from an interface
func CreateSingletonList(in interface{}) []interface{} {
	return []interface{}{in}
}

// YbaStructuredError is a structure mimicking YBPError, with error being an interface{}
// to accomodate errors thrown as YBPStructuredError
type YbaStructuredError struct {
	// User-visible unstructured error message
	Error *interface{} `json:"error,omitempty"`
	// Method for HTTP call that resulted in this error
	HTTPMethod *string `json:"httpMethod,omitempty"`
	// URI for HTTP request that resulted in this error
	RequestURI *string `json:"requestUri,omitempty"`
	// Mostly set to false to indicate failure
	Success *bool `json:"success,omitempty"`
}

// ErrorFromHTTPResponse extracts the error message from the HTTP response of the API
func ErrorFromHTTPResponse(resp *http.Response, apiError error, entityName,
	operation string) error {
	errorTag := fmt.Errorf("%s, Operation: %s - %w", entityName, operation, apiError)
	if resp == nil {
		return errorTag
	}
	response := *resp
	errorBlock := YbaStructuredError{}
	body, err := io.ReadAll(response.Body)
	if err != nil {
		logrus.Debug("There was an error reading the response from the API\n")
		return errorTag
	}
	if err = json.Unmarshal(body, &errorBlock); err != nil {
		logrus.Debugf("There was an error unmarshalling the response from the API\n")
		return errorTag
	}
	errorString := ErrorFromResponseBody(errorBlock)
	return fmt.Errorf("%w: %s", errorTag, errorString)
}

// ErrorFromResponseBody is a function to extract error interfaces into string
func ErrorFromResponseBody(errorBlock YbaStructuredError) string {
	var errorString string
	if reflect.TypeOf(*errorBlock.Error) == reflect.TypeOf(errorString) {
		return (*errorBlock.Error).(string)
	}

	errorMap := (*errorBlock.Error).(map[string]interface{})
	for k, v := range errorMap {
		if k != "" {
			errorString = fmt.Sprintf("Field: %s, Error:", k)
		}
		var checkType []interface{}
		if reflect.TypeOf(v) == reflect.TypeOf(checkType) {
			for _, s := range *StringSlice(v.([]interface{})) {
				errorString = fmt.Sprintf("%s %s", errorString, s)
			}
		} else {
			errorString = fmt.Sprintf("%s %s", errorString, v.(string))
		}

	}
	return errorString
}

// ConfirmCommand function will add an interactive comfirmation with the message provided
func ConfirmCommand(message string, bypass bool) error {
	errAborted := fmt.Errorf("command aborted")
	if bypass {
		return nil
	}
	response := false
	prompt := &survey.Confirm{
		Message: message,
	}
	err := survey.AskOne(prompt, &response)
	if err != nil {
		return err
	}
	if !response {
		return errAborted
	}
	return nil
}

// CompareYbVersions returns -1 if version1 < version2, 0 if version1 = version2,
// 1 if version1 > version2
func CompareYbVersions(v1 string, v2 string) (int, error) {
	ybaVersionRegex := "^(\\d+.\\d+.\\d+.\\d+)(-(b(\\d+)|(\\w+)))?$"
	// After the second dash, a user can add anything, and it will be ignored.
	v1Parts := strings.Split(v1, "-")
	if len(v1Parts) > 2 {
		v1 = fmt.Sprintf("%v%v", v1Parts[0]+"-", v1Parts[1])
	}
	v2Parts := strings.Split(v2, "-")
	if len(v2Parts) > 2 {
		v2 = fmt.Sprintf("%v%v", v2Parts[0]+"-", v2Parts[1])
	}
	versionPattern, err := regexp.Compile(ybaVersionRegex)
	if err != nil {
		return 0, err
	}
	v1Matcher := versionPattern.Match([]byte(v1))
	v2Matcher := versionPattern.Match([]byte(v2))
	if v1Matcher && v2Matcher {
		v1Groups := versionPattern.FindAllStringSubmatch(v1, -1)
		v2Groups := versionPattern.FindAllStringSubmatch(v2, -1)
		v1Numbers := strings.Split(v1Groups[0][1], ".")
		v2Numbers := strings.Split(v2Groups[0][1], ".")
		for i := 0; i < 4; i++ {
			var err error
			a, err := strconv.Atoi(v1Numbers[i])
			if err != nil {
				return 0, err
			}
			b, err := strconv.Atoi(v2Numbers[i])
			if err != nil {
				return 0, err
			}
			if a > b {
				return 1, nil
			} else if a < b {
				return -1, nil
			}
		}
		v1BuildNumber := v1Groups[0][4]
		v2BuildNumber := v2Groups[0][4]
		// If one of the build number is null (i.e local build) then consider
		// versions as equal as we cannot compare between local builds
		// e.g: 2.5.2.0-b15 and 2.5.2.0-custom are considered equal
		// 2.5.2.0-custom1 and 2.5.2.0-custom2 are considered equal too
		if v1BuildNumber != "" && v2BuildNumber != "" {
			var err error
			a, err := strconv.Atoi(v1BuildNumber)
			if err != nil {
				return 0, err
			}
			b, err := strconv.Atoi(v2BuildNumber)
			if err != nil {
				return 0, err
			}
			if a > b {
				return 1, nil
			} else if a < b {
				return -1, nil
			} else {
				return 0, nil
			}
		}
		return 0, nil
	}
	return 0, errors.New("Unable to parse YB version strings")
}

func IsVersionStable(version string) bool {
	v := strings.Split(version, ".")
	v1, err := strconv.Atoi(v[1])
	if err != nil {
		logrus.Error("Unable to parse YB version strings")
		return false
	}
	return v1%2 == 0 || len(v[0]) == 4
}

// IsYBVersion checks if the given string is a valid YB version string
func IsYBVersion(v string) (bool, error) {
	ybaVersionRegex := "^(\\d+.\\d+.\\d+.\\d+)(-(b(\\d+)|(\\w+)))?$"
	vParts := strings.Split(v, "-")
	if len(vParts) > 2 {
		v = fmt.Sprintf("%v%v", vParts[0]+"-", vParts[1])
	}
	versionPattern, err := regexp.Compile(ybaVersionRegex)
	if err != nil {
		return false, err
	}
	vMatcher := versionPattern.Match([]byte(v))
	if !vMatcher {
		return false, errors.New("unable to parse YB version strings")
	}
	return true, nil
}

// YAMLtoString reads yaml file and converts the data into a string
func YAMLtoString(filePath string) string {
	logrus.Debug("YAML File Path: ", filePath)
	yamlContent, err := os.ReadFile(filePath)
	if err != nil {
		logrus.Fatalf(
			formatter.Colorize("Error reading YAML file: "+err.Error()+"\n",
				formatter.RedColor))
	}
	var data map[string]interface{}

	// Unmarshal the YAML content into the map
	err = yaml.Unmarshal(yamlContent, &data)
	if err != nil {
		logrus.Fatalf(
			formatter.Colorize("Error unmarshalling YAML file: "+err.Error()+"\n",
				formatter.RedColor))
	}

	contentBytes, err := yaml.Marshal(data)
	if err != nil {
		logrus.Fatalf(
			formatter.Colorize("Error marshalling YAML file: "+err.Error()+"\n",
				formatter.RedColor))
	}
	return string(contentBytes)

}

// IsOutputType check if the output type is t
func IsOutputType(t string) bool {
	return viper.GetString("output") == t
}
