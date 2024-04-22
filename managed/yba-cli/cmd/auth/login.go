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

// LoginCmd uses user name and password to configure the CLI
var LoginCmd = &cobra.Command{
	Use:   "login",
	Short: "Authenticate yba cli using email and password",
	Long: "Connect to YugabyteDB Anywhere host machine using email and password." +
		" If non-interactive mode is set, provide the host, email and password using flags. " +
		"Default for host is \"http://localhost:9000\"",
	Run: func(cmd *cobra.Command, args []string) {
		force, err := cmd.Flags().GetBool("force")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
		var email, password string
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
					formatter.Colorize("Could not read host: "+err.Error(), formatter.RedColor))
			}
			// If the input is just a newline, use the default value
			if input == "\n" {
				input = hostConfig + "\n"
			}
			host = strings.TrimSpace(input)
			if len(host) == 0 {
				if len(strings.TrimSpace(hostConfig)) == 0 {
					logrus.Fatalln(formatter.Colorize("Host cannot be empty.", formatter.RedColor))
				} else {
					host = hostConfig
				}
			}

			// Prompt for the email
			fmt.Print("Enter email or username: ")
			_, err = fmt.Scanln(&email)
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read email: "+err.Error(), formatter.RedColor))
			}

			// Prompt for the password
			fmt.Print("Enter password: ")
			data, err = term.ReadPassword(int(os.Stdin.Fd()))
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read password: "+err.Error(), formatter.RedColor))
			}
			password = string(data)

		} else {
			hostFlag, err := cmd.Flags().GetString("host")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
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
			email, err = cmd.Flags().GetString("email")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
			}
			password, err = cmd.Flags().GetString("password")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
			}
		}

		viper.GetViper().Set("host", &host)
		if strings.TrimSpace(email) == "" {
			logrus.Fatalln(formatter.Colorize("Email cannot be empty.", formatter.RedColor))
		}

		if strings.TrimSpace(password) == "" {
			logrus.Fatalln(formatter.Colorize("Password cannot be empty.", formatter.RedColor))
		}

		fmt.Println("\n")

		url, err := ybaAuthClient.ParseURL(host)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		authAPI, err := ybaAuthClient.NewAuthAPIClientInitialize(url, "")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		req := ybaclient.CustomerLoginFormData{
			Email:    email,
			Password: password,
		}

		r, response, err := authAPI.ApiLogin().CustomerLoginFormData(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Login", "Authentication")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		logrus.Debugf("API Login response without errors\n")

		apiToken := r.GetApiToken()
		viper.GetViper().Set("apiToken", &apiToken)

		authUtil(url, apiToken)
	},
}

func init() {
	LoginCmd.Flags().SortFlags = false
	LoginCmd.Flags().StringP("email", "e", "",
		fmt.Sprintf(
			"[Optional] Email or username for the user. %s",
			formatter.Colorize("Required for non-interactive usage", formatter.GreenColor)))
	LoginCmd.Flags().StringP("password", "p", "",
		fmt.Sprintf(
			"[Optional] Password for the user. %s",
			formatter.Colorize("Required for non-interactive usage", formatter.GreenColor)))
	LoginCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
