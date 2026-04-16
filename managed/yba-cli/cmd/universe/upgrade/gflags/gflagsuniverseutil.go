/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gflags

import (
	"fmt"
	"slices"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

func validateGFlags(
	authAPI *client.AuthAPIClient,
	universeName string,
	universeUUID string,
	version string,
	cliSpecificGFlags []util.SpecificGFlagsCLIOutput) []string {
	validateGFlags := make([]ybaclient.GFlagsValidationRequest, 0)

	perProcessFlagValuesList := make([]util.PerProcessFlags, 0)
	for _, gflagStructure := range cliSpecificGFlags {
		gflags := gflagStructure.SpecificGFlags
		perProcessFlagValuesList = append(perProcessFlagValuesList, *gflags.PerProcessFlags)
		for _, v := range *gflags.PerAZ {
			perProcessFlagValuesList = append(perProcessFlagValuesList, v)
		}
	}

	for _, perProcessFlagValues := range perProcessFlagValuesList {

		masterValues := perProcessFlagValues[util.MasterServerType]
		tserverValues := perProcessFlagValues[util.TserverServerType]

		visted := make([]string, 0)

		for k, v := range masterValues {
			validate := ybaclient.GFlagsValidationRequest{}
			validate.SetName(k)
			validate.SetMASTER(v)
			if tserverValue, ok := tserverValues[k]; ok {
				validate.SetTSERVER(tserverValue)
			}
			visted = append(visted, k)
			validateGFlags = append(validateGFlags, validate)
		}
		for k, v := range tserverValues {
			if !slices.Contains(visted, k) {
				validate := ybaclient.GFlagsValidationRequest{}
				validate.SetName(k)
				validate.SetTSERVER(v)
				visted = append(visted, k)
				validateGFlags = append(validateGFlags, validate)
			}
		}
	}

	if len(validateGFlags) == 0 {
		logrus.Fatalf(
			formatter.Colorize(
				"No gflags found to validate. "+
					"Provide specific-gflags or specific-gflags-file-path\n",
				formatter.RedColor))
	}

	req := ybaclient.GFlagsValidationFormData{
		Gflags: validateGFlags,
	}

	rValidate, response, err := authAPI.ValidateGFlags(version).
		GflagValidationFormData(req).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "Universe", "Validate GFlags")
	}

	logrus.Info(
		fmt.Sprintf("Successfully validated universe %s (%s) gflags\n",
			formatter.Colorize(universeName, formatter.GreenColor),
			universeUUID,
		))

	errors := make([]string, 0)

	for _, gflag := range rValidate {
		name := gflag.GetName()
		master := gflag.GetMASTER()
		tserver := gflag.GetTSERVER()
		if len(master.GetError()) != 0 {
			err := fmt.Sprintf("GFlag %s master value: %s", name, master.GetError())
			errors = append(errors, err)
		}
		if len(tserver.GetError()) != 0 {
			err := fmt.Sprintf("GFlag %s tserver value: %s", name, tserver.GetError())
			errors = append(errors, err)
		}

		if !master.GetExist() || !tserver.GetExist() {
			err := fmt.Sprintf("GFlag %s does not exist", name)
			errors = append(errors, err)
		}
	}
	return errors
}
