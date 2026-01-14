/*
* Copyright (c) YugabyteDB, Inc.
 */

package auth

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"golang.org/x/term"
)

// RegisterCmd to create a new customer
var RegisterCmd = &cobra.Command{
	Use:   "register",
	Short: "Register a YugabyteDB Anywhere customer using yba cli",
	Long: "Register a YugabyteDB Anywhere customer using yba cli. " +
		"If non-interactive mode is set, provide the host, name, email, password" +
		" and environment using flags.",
	Example: "yba register -f -n <name> -e <email> -p <password> -H <host>",
	Run: func(cmd *cobra.Command, args []string) {
		force, err := cmd.Flags().GetBool("force")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var email, password, confirmPassword string
		var name, code string
		var data []byte
		url := ViperVariablesInAuth(cmd, force)
		if !force {
			// Prompt for the name
			fmt.Printf("Enter name: ")
			readerName := bufio.NewReader(os.Stdin)
			inputName, err := readerName.ReadString('\n')
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read name: "+err.Error()+"\n",
						formatter.RedColor))
			}
			name = strings.TrimSpace(inputName)

			fmt.Printf("Enter environment [dev]: ")
			// Prompt for the code
			readerCode := bufio.NewReader(os.Stdin)
			inputCode, err := readerCode.ReadString('\n')
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read environment: "+err.Error()+"\n",
						formatter.RedColor))
			}
			code = strings.TrimSpace(inputCode)
			if len(code) == 0 {
				code = "dev"
			}

			// Prompt for the email
			fmt.Print("Enter email: ")
			_, err = fmt.Scanln(&email)
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read email: "+err.Error()+"\n",
						formatter.RedColor))
			}

			// Prompt for the password
			fmt.Print(
				"Enter password (must contain at least 8 characters " +
					"and at least 1 digit , 1 capital , 1 lowercase and 1 " +
					"of the !@#$^&* (special) characters): ")
			data, err = term.ReadPassword(int(os.Stdin.Fd()))
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read password: "+err.Error()+"\n",
						formatter.RedColor))
			}
			password = string(data)

			// Prompt for confirm password
			fmt.Print("Confirm entered password: ")
			data, err = term.ReadPassword(int(os.Stdin.Fd()))
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read password: "+err.Error()+"\n",
						formatter.RedColor))
			}
			confirmPassword = string(data)

			if !strings.EqualFold(password, confirmPassword) {
				logrus.Fatalln(
					formatter.Colorize("Passwords do not match.\n",
						formatter.RedColor))
			}

		} else {
			name, err = cmd.Flags().GetString("name")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			email, err = cmd.Flags().GetString("email")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			password, err = cmd.Flags().GetString("password")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			code, err = cmd.Flags().GetString("environment")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		if util.IsEmptyString(name) {
			logrus.Fatalln(
				formatter.Colorize(
					"Name cannot be empty.\n",
					formatter.RedColor))
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

		req := ybaclient.CustomerRegisterFormData{
			Email:    email,
			Password: password,
			Name:     name,
			Code:     code,
		}

		r, response, err := authAPI.RegisterCustomer().CustomerRegisterFormData(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Register", "Authentication")
		}
		logrus.Debugf("API Register response without errors\n")

		reqLogin := ybaclient.CustomerLoginFormData{
			Email:    email,
			Password: password,
		}

		r, response, err = authAPI.ApiLogin().CustomerLoginFormData(reqLogin).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Register", "Login")
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
	RegisterCmd.Flags().SortFlags = false
	RegisterCmd.Flags().StringP("name", "n", "",
		fmt.Sprintf(
			"[Optional] Name of the user. %s",
			formatter.Colorize("Required for non-interactive usage", formatter.GreenColor)))
	RegisterCmd.Flags().StringP("email", "e", "",
		fmt.Sprintf(
			"[Optional] Email for the user. %s",
			formatter.Colorize("Required for non-interactive usage", formatter.GreenColor)))
	RegisterCmd.Flags().StringP("password", "p", "",
		fmt.Sprintf(
			"[Optional] Password for the user. Password must contain at "+
				"least 8 characters and at least 1 digit , 1 capital , 1 lowercase"+
				" and 1 of the !@#$^&* (special) characters. %s. Use single quotes ('') to provide "+
				"values with special characters.",
			formatter.Colorize("Required for non-interactive usage", formatter.GreenColor)))
	RegisterCmd.Flags().String("environment", "dev",
		"[Optional] Environment of the installation. "+
			"Allowed values: dev, demo, stage, prod.")
	RegisterCmd.Flags().
		Bool("show-api-token", false, "[Optional] Show the API token after registeration. (default false)")
	RegisterCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
