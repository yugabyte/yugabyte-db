package checks

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"

	"github.com/spf13/viper"
	"github.com/xeipuuv/gojsonschema"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"sigs.k8s.io/yaml"
)

var ValidateInstallerConfig = &validateConfigCheck{"validate-config", true}
var ValidateLocaleConfig = &validateLocaleConfig{"validate-locale-config", true}

type validateConfigCheck struct {
	name        string
	skipAllowed bool
}

func (s validateConfigCheck) Name() string {
	return s.name
}

func (s validateConfigCheck) SkipAllowed() bool {
	return s.skipAllowed
}

// Execute runs the check. Will validate there is enough disk space
func (s validateConfigCheck) Execute() Result {
	res := Result{
		Check:  s.name,
		Status: StatusPassed,
	}

	res.Error = validateJSONSchema()
	if res.Error != nil {
		res.Status = StatusCritical
	}
	return res
}

// ValidateJSONSchema checks that the parameters in each component's config file are indeed
// valid by turning the input YAML file into a JSON file, and then validating that
// the parameters have been specified appropriately using the available
// JSON schema.
func validateJSONSchema() error {

	createdBytes, err := os.ReadFile(common.InputFile())
	if err != nil {
		log.Fatal(fmt.Sprintf("Error: %v.", err))
	}

	jsonString, jsonStringErr := yaml.YAMLToJSON(createdBytes)
	if jsonStringErr != nil {
		return fmt.Errorf("Error: %v.\n", jsonStringErr)
	}

	var jsonData map[string]interface{}
	if jsonDataError := json.Unmarshal([]byte(jsonString), &jsonData); jsonDataError != nil {
		return fmt.Errorf("Error: %v.\n", jsonDataError)
	}

	jsonBytesInput, _ := json.Marshal(jsonData)

	jsonStringInput := string(jsonBytesInput)

	configDirPath := common.GetTemplatesDir()

	jsonSchemaName := fmt.Sprintf("file://%s/yba-installer-input-json-schema.json", configDirPath)

	schemaLoader := gojsonschema.NewReferenceLoader(jsonSchemaName)
	documentLoader := gojsonschema.NewStringLoader(jsonStringInput)

	result, err := gojsonschema.Validate(schemaLoader, documentLoader)

	// Panic to automatically exit the Templating Phase if the passed-in parameters are
	// not valid.
	if err != nil {
		return err
	}

	if !result.Valid() {
		errMsg := "The config at " + common.InputFile() + " is not valid. Errors: \n"
		for _, desc := range result.Errors() {
			errMsg += fmt.Sprintf("- %s\n", desc)
		}
		log.Info(errMsg)
		return fmt.Errorf(errMsg)
	}

	log.Info("Config at " + common.InputFile() + " was found to be valid.")
	return nil
}

type validateLocaleConfig struct {
	name        string
	skipAllowed bool
}

func (l validateLocaleConfig) Name() string {
	return l.name
}

func (l validateLocaleConfig) SkipAllowed() bool {
	return l.skipAllowed
}

func (l validateLocaleConfig) Execute() Result {
	res := Result{
		Check:  l.name,
		Status: StatusPassed,
	}

	res.Error = validateLocaleExists()
	if res.Error != nil {
		res.Status = StatusCritical
	}
	return res
}

// validateLocaleExists ensures that the postgres.install.locale specified exists on the system.
func validateLocaleExists() error {
	locale := viper.GetString("postgres.install.locale")
	// Skip the test of no locale is given.
	if locale == "" {
		log.Debug("skipping locale check as non was given")
		return nil
	}
	cmd := fmt.Sprintf("locale -a | grep -i \"^%s$\"", locale)
	out := shell.RunShell(cmd)
	if out.ExitCode != 0 {
		return errors.New(fmt.Sprintf(
			"locale %s does not exist. install the locale or update %s config to a "+
				"locale found in 'locale -a'", locale, common.InputFile()))
	}
	return nil
}
