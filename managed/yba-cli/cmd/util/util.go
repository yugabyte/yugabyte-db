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
	"time"

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

// Float64Slice accepts array of interface and returns a pointer to slice of float64
func Float64Slice(in []interface{}) *[]float64 {
	var out []float64
	for _, v := range in {
		out = append(out, v.(float64))
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

// GetArrayPointer returns the pointer to a string array
func GetArrayPointer(in []interface{}) *[]interface{} {
	return &in
}

// CreateSingletonList returns a list of single entry from an interface
func CreateSingletonList(in interface{}) []interface{} {
	return []interface{}{in}
}

// FindCommonStringElements finds common elements in two string slices
func FindCommonStringElements(list1, list2 []string) []string {
	// Create a map to store elements from list1
	elementMap := make(map[string]bool)
	for _, val := range list1 {
		elementMap[val] = true
	}

	// Find common elements
	var common []string
	for _, val := range list2 {
		if elementMap[val] {
			common = append(common, val)
		}
	}
	return common
}

// GetFloat64SliceFromString returns a slice of float64 from a string
func GetFloat64SliceFromString(in string) ([]float64, error) {
	if in == "" {
		return nil, nil
	}
	in = strings.Trim(in, "[ ]")
	s := strings.Split(in, ",")
	var out []float64
	for _, v := range s {
		f, err := strconv.ParseFloat(v, 64)
		if err != nil {
			return nil, err
		}
		out = append(out, f)
	}
	return out, nil
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
		var checkTypeMap map[string]interface{}
		if reflect.TypeOf(v) == reflect.TypeOf(checkType) {
			for _, s := range *StringSlice(v.([]interface{})) {
				errorString = fmt.Sprintf("%s %s", errorString, s)
			}
		} else if reflect.TypeOf(v) == reflect.TypeOf(checkTypeMap) {
			for _, s := range *StringMap(v.(map[string]interface{})) {
				errorString = fmt.Sprintf("%s %s", errorString, s)
			}
			errorString = fmt.Sprintf("%s %v", errorString, v)
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

// IsVersionStable returns true if the version string is stable
// A stable version is a version with an even minor number
// or a version with a 4 digit major version
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
	var data yaml.MapSlice

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

// RemoveComponentFromSlice removes the component from the slice
func RemoveComponentFromSlice(sliceInterface interface{}, index int) interface{} {
	slice := sliceInterface.([]interface{})
	length := len(slice)
	for i := range slice {
		if i == index && i != length-1 {
			return append(slice[:i], slice[i+1:]...)
		} else if i == length-1 {
			return slice[:i]
		}
	}
	return slice
}

// ConvertMsToUnit converts time from milliseconds to unit
func ConvertMsToUnit(value int64, unit string) float64 {
	var v float64
	if strings.Compare(unit, "YEARS") == 0 {
		v = (float64(value) / 12 / 30 / 24 / 60 / 60 / 1000)
	} else if strings.Compare(unit, "MONTHS") == 0 {
		v = (float64(value) / 30 / 24 / 60 / 60 / 1000)
	} else if strings.Compare(unit, "DAYS") == 0 {
		v = (float64(value) / 24 / 60 / 60 / 1000)
	} else if strings.Compare(unit, "HOURS") == 0 {
		v = (float64(value) / 60 / 60 / 1000)
	} else if strings.Compare(unit, "MINUTES") == 0 {
		v = (float64(value) / 60 / 1000)
	} else if strings.Compare(unit, "SECONDS") == 0 {
		v = (float64(value) / 1000)
	}
	return v
}

// GetUnitOfTimeFromDuration takes time.Duration as input and caluclates the unit specified in
// that duration
func GetUnitOfTimeFromDuration(duration time.Duration) string {
	if duration.Hours() >= float64(24*30*365) {
		return "YEARS"
	} else if duration.Hours() >= float64(24*30) {
		return "MONTHS"
	} else if duration.Hours() >= float64(24) {
		return "DAYS"
	} else if duration.Hours() >= float64(1) {
		return "HOURS"
	} else if duration.Minutes() >= float64(1) {
		return "MINUTES"
	} else if duration.Seconds() >= float64(1) {
		return "SECONDS"
	} else if duration.Milliseconds() > int64(0) {
		return "MILLISECONDS"
	} else if duration.Microseconds() > int64(0) {
		return "MICROSECONDS"
	} else if duration.Nanoseconds() > int64(0) {
		return "NANOSECONDS"
	}
	return ""
}

// GetMsFromDurationString retrieves the ms notation of the duration mentioned in the input string
// return value string holds the unit calculated from time.Duration
// Throws error on improper duration format
func GetMsFromDurationString(duration string) (int64, string, bool, error) {
	number, err := time.ParseDuration(duration)
	if err != nil {
		return 0, "", false, err
	}
	unitFromDuration := GetUnitOfTimeFromDuration(number)
	return number.Milliseconds(), unitFromDuration, true, err
}

// FromEpochMilli converts epoch in milliseconds to time.Time
func FromEpochMilli(millis int64) time.Time {
	// Convert milliseconds to seconds and nanoseconds
	seconds := millis / 1000
	nanos := (millis % 1000) * int64(time.Millisecond)
	return time.Unix(seconds, nanos)
}
