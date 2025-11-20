/*
 * Copyright (c) YugabyteDB, Inc.
 */

package auth

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"golang.org/x/term"
)

// LoginCmd uses user name and password to configure the CLI
var LoginCmd = &cobra.Command{
	Use:   "login",
	Short: "Authenticate yba cli using email and password",
	Long: "Connect to YugabyteDB Anywhere host machine using email and password." +
		" If non-interactive mode is set, provide the host, email and password using flags. " +
		"Default for host is \"http://localhost:9000\".",
	Example: "yba login -f -e <email> -p <password> -H <host>",
	Run: func(cmd *cobra.Command, args []string) {
		force, err := cmd.Flags().GetBool("force")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var email, password string
		var data []byte
		url := ViperVariablesInAuth(cmd, force)
		if !force {
			// Prompt for the email
			fmt.Print("Enter email or username: ")
			_, err = fmt.Scanln(&email)
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read email: "+err.Error()+"\n",
						formatter.RedColor))
			}

			// Prompt for the password
			fmt.Print("Enter password: ")
			data, err = term.ReadPassword(int(os.Stdin.Fd()))
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read password: "+err.Error()+"\n",
						formatter.RedColor))
			}
			password = string(data)

		} else {
			email, err = cmd.Flags().GetString("email")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			password, err = cmd.Flags().GetString("password")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		if util.IsEmptyString(email) {
			logrus.Fatalln(
				formatter.Colorize(
					"Email cannot be empty.\n",
					formatter.RedColor))
		}

		if util.IsEmptyString(password) {
			logrus.Fatalln(
				formatter.Colorize(
					"Password cannot be empty.\n",
					formatter.RedColor))
		}

		logrus.Infoln("\n")

		authAPI, err := ybaAuthClient.NewAuthAPIClientInitialize(url, "")
		if err != nil {
			logrus.Fatal(
				formatter.Colorize(
					err.Error()+"\n",
					formatter.RedColor))
		}

		req := ybaclient.CustomerLoginFormData{
			Email:    email,
			Password: password,
		}

		r, response, err := authAPI.ApiLogin().CustomerLoginFormData(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Login", "Authentication")
		}
		logrus.Debugf("API Login response without errors\n")

		showToken, err := cmd.Flags().GetBool("show-api-token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		InitializeAuthenticatedSession(url, r.GetApiToken(), showToken)
	},
}

func init() {
	LoginCmd.Flags().SortFlags = false
	LoginCmd.Flags().StringP("email", "e", "",
		fmt.Sprintf(
			"[Optional] Email or username for the user. %s.",
			formatter.Colorize("Required for non-interactive usage", formatter.GreenColor)))
	LoginCmd.Flags().StringP("password", "p", "",
		fmt.Sprintf(
			"[Optional] Password for the user. %s. Use single quotes ('') to provide "+
				"values with special characters.",
			formatter.Colorize("Required for non-interactive usage", formatter.GreenColor)))
	LoginCmd.Flags().
		Bool("show-api-token", false, "[Optional] Show the API token after login. (default false)")

	LoginCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
