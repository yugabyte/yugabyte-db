/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"golang.org/x/term"
)

var authCmd = &cobra.Command{
	Use:   "auth",
	Short: "Authenticate yba cli",
	Long:  "Authenticate the yba cli through this command by providing the host and API Token.",
	Run: func(cmd *cobra.Command, args []string) {
		var apiToken string
		var host string
		var data []byte
		var err error

		// Prompt for the host
		fmt.Print("Enter Host: ")
		_, err = fmt.Scanln(&host)
		if err != nil {
			logrus.Fatalln(
				formatter.Colorize("Could not read host: "+err.Error(), formatter.RedColor))
		}
		if len(host) == 0 {
			logrus.Fatalln(formatter.Colorize("Host cannot be empty.", formatter.RedColor))
		}
		viper.GetViper().Set("host", &host)

		// Prompt for the API token
		fmt.Print("Enter API Token: ")
		data, err = term.ReadPassword(int(os.Stdin.Fd()))
		if err != nil {
			logrus.Fatalln(
				formatter.Colorize("Could not read apiToken: "+err.Error(), formatter.RedColor))
		}
		apiToken = string(data)

		// Validate that apiToken is a valid JWT token and that the token is not expired
		if strings.TrimSpace(apiToken) == "" {
			logrus.Fatalln(formatter.Colorize("apiToken cannot be empty.", formatter.RedColor))
		}
		viper.GetViper().Set("apiToken", &apiToken)

		// Before writing the config, validate that the data is correct
		url, err := ybaAuthClient.ParseURL(host)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		authAPI, _ := ybaAuthClient.NewAuthAPIClientInitialize(url, apiToken)
		_, response, err := authAPI.GetSessionInfo().Execute()
		if err != nil {
			logrus.Debugf("Full HTTP response: %v\n", response)
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Get Session Info", "Read")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))

		}
		logrus.Debugf("Session Info response without errors\n")

		// Fetch Customer UUID
		err = authAPI.GetCustomerUUID()
		if err != nil {
			logrus.Debugf("Full HTTP response: %v\n", response)
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Get Session Info", "Read")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))

		}
		logrus.Debugf("Fetched Customer UUID without errors.\n")

		err = viper.WriteConfig()
		if err != nil {
			if _, ok := err.(viper.ConfigFileNotFoundError); ok {
				fmt.Fprintln(os.Stdout, "No config was found a new one will be created.")
				//Try to create the file
				err = viper.SafeWriteConfig()
				if err != nil {
					logrus.Fatalf(
						formatter.Colorize(
							"Error when writing new config file: %v\n"+err.Error(),
							formatter.RedColor))

				}
			} else {
				logrus.Fatalf(
					formatter.Colorize(
						"Error when writing config file: %v\n"+err.Error(), formatter.RedColor))
			}
		}
		configFileUsed := viper.GetViper().ConfigFileUsed()
		if len(configFileUsed) > 0 {
			logrus.Infof("Configuration file '%v' sucessfully updated.\n",
				configFileUsed)
		} else {
			configFileUsed = "$HOME/.yba-cli.yaml"
			logrus.Infof("Configuration file '%v' sucessfully updated.\n",
				configFileUsed)
		}
	},
}
