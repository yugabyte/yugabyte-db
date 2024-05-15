/*
* Copyright (c) YugaByte, Inc.
 */

package auth

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
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
	Run: func(cmd *cobra.Command, args []string) {
		force, err := cmd.Flags().GetBool("force")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var email, password, confirmPassword string
		var name, code string
		var host string
		var data []byte
		hostConfig := viper.GetString("host")
		if !force {
			fmt.Printf("Enter Host [%s]: ", hostConfig)
			// Prompt for the host
			reader := bufio.NewReader(os.Stdin)
			input, err := reader.ReadString('\n')
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read host: "+err.Error()+"\n",
						formatter.RedColor))
			}
			// If the input is just a newline, use the default value
			if input == "\n" {
				input = hostConfig + "\n"
			}
			host = strings.TrimSpace(input)
			if len(host) == 0 {
				if len(strings.TrimSpace(hostConfig)) == 0 {
					logrus.Fatalln(
						formatter.Colorize("Host cannot be empty.\n",
							formatter.RedColor))
				} else {
					host = hostConfig
				}
			}

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
			fmt.Print("Enter password: ")
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
			hostFlag, err := cmd.Flags().GetString("host")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			// If the host is empty
			if strings.Compare(hostFlag, "http://localhost:9000") == 0 {
				if len(strings.TrimSpace(hostConfig)) == 0 {
					host = hostFlag
				} else {
					host = hostConfig
				}
			} else {
				host = hostFlag
			}
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

		viper.GetViper().Set("host", &host)

		if strings.TrimSpace(name) == "" {
			logrus.Fatalln(
				formatter.Colorize(
					"Name cannot be empty.\n",
					formatter.RedColor))
		}

		if strings.TrimSpace(email) == "" {
			logrus.Fatalln(
				formatter.Colorize(
					"Email cannot be empty.\n",
					formatter.RedColor))
		}

		if strings.TrimSpace(password) == "" {
			logrus.Fatalln(
				formatter.Colorize(
					"Password cannot be empty.\n",
					formatter.RedColor))
		}

		logrus.Infoln("\n")

		url, err := ybaAuthClient.ParseURL(host)
		if err != nil {
			logrus.Fatal(
				formatter.Colorize(
					err.Error()+"\n",
					formatter.RedColor))
		}

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
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Register", "Authentication")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		logrus.Debugf("API Register response without errors\n")

		reqLogin := ybaclient.CustomerLoginFormData{
			Email:    email,
			Password: password,
		}

		r, response, err = authAPI.ApiLogin().CustomerLoginFormData(reqLogin).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Register", "Login")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		logrus.Debugf("API Login response without errors\n")

		apiToken := r.GetApiToken()
		viper.GetViper().Set("apiToken", &apiToken)

		authUtil(url, apiToken)
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
				" and 1 of the !@#$^&* (special) characters. %s",
			formatter.Colorize("Required for non-interactive usage", formatter.GreenColor)))
	RegisterCmd.Flags().String("environment", "dev",
		"[Optional] Environment of the installation. "+
			"Allowed values: dev, demo, stage, prod.")
	RegisterCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
