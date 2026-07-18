/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetypeutil

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// RemoveInstanceTypeUtil removes instance type
func RemoveInstanceTypeUtil(
	cmd *cobra.Command,
	providerType string,
	providerTypeInLog string,
	providerTypeInMessage string,
) {
	callSite := "Instance Type"
	if len(providerTypeInLog) > 0 {
		callSite = fmt.Sprintf("%s: %s", callSite, providerTypeInLog)
	}
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	providerName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	providerListRequest := authAPI.GetListOfProviders()
	providerListRequest = providerListRequest.Name(providerName).
		ProviderCode(providerType)
	r, response, err := providerListRequest.Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Remove - Get Provider")
	}
	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf(
					"No %s providers with name: %s found\n",
					providerTypeInMessage,
					providerName,
				),
				formatter.RedColor,
			))
	}

	providerUUID := r[0].GetUuid()

	instanceTypeName, err := cmd.Flags().GetString("instance-type-name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	rDelete, response, err := authAPI.DeleteInstanceType(providerUUID, instanceTypeName).
		Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Remove")
	}

	if rDelete.GetSuccess() {
		logrus.Infof("The instance type %s has been removed from provider %s (%s)\n",
			formatter.Colorize(instanceTypeName, formatter.GreenColor),
			providerName,
			providerUUID)

	} else {
		logrus.Errorf(
			formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while removing instance type %s from provider %s (%s)\n",
					instanceTypeName,
					providerName,
					providerUUID),
				formatter.RedColor))
	}
}

// DeleteInstanceTypeValidations deletes instance type
func DeleteInstanceTypeValidations(cmd *cobra.Command) {
	viper.BindPFlag("force", cmd.Flags().Lookup("force"))
	DescribeAndRemoveInstanceTypeValidations(cmd, "remove")
	instanceTypeName, err := cmd.Flags().GetString("instance-type-name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	err = util.ConfirmCommand(
		fmt.Sprintf(
			"Are you sure you want to remove %s: %s",
			"instance type",
			instanceTypeName,
		),
		viper.GetBool("force"),
	)
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}
