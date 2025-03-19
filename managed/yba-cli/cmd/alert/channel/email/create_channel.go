/*
 * Copyright (c) YugaByte, Inc.
 */

package email

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"

	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createEmailChannelAlertCmd represents the create alert command
var createEmailChannelAlertCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a email alert channel in YugabyteDB Anywhere",
	Long:    "Create a email alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel email create --name <alert-channel-name> \
  --use-default-recipients --use-default-smtp-settings`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "create")
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		useDefaultRecipients, err := cmd.Flags().GetBool("use-default-rececipients")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		recipients := make([]string, 0)
		if !useDefaultRecipients {
			recipients, err = cmd.Flags().GetStringArray("recipients")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(recipients) == 0 {
				logrus.Fatal(
					formatter.Colorize(
						"Recipients are required when use-default-recipients is false\n",
						formatter.RedColor,
					),
				)
			}
		}

		useDefaultSMTPSettings, err := cmd.Flags().GetBool("use-default-smtp-settings")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		reqBody := util.AlertChannelFormData{
			Name: name,
			Params: util.AlertChannelParams{
				ChannelType:         util.GetStringPointer(util.EmailAlertChannelType),
				Recipients:          util.StringSliceFromString(recipients),
				DefaultRecipients:   util.GetBoolPointer(useDefaultRecipients),
				DefaultSmtpSettings: util.GetBoolPointer(useDefaultSMTPSettings),
			},
		}
		if !useDefaultSMTPSettings {
			emailFrom, err := cmd.Flags().GetString("email-from")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if emailFrom == "" {
				logrus.Fatal(
					formatter.Colorize(
						"Email from is required when use-default-smtp-settings is false\n",
						formatter.RedColor,
					),
				)
			}
			server, err := cmd.Flags().GetString("smtp-server")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if server == "" {
				logrus.Fatal(
					formatter.Colorize(
						"SMTP server is required when use-default-smtp-settings is false\n",
						formatter.RedColor,
					),
				)
			}
			port, err := cmd.Flags().GetInt("smtp-port")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if port == -1 {
				logrus.Fatal(
					formatter.Colorize(
						"SMTP port is required when use-default-smtp-settings is false\n",
						formatter.RedColor,
					),
				)
			}
			useSSL, err := cmd.Flags().GetBool("use-ssl")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			useTLS, err := cmd.Flags().GetBool("use-tls")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			smtpUsername, err := cmd.Flags().GetString("smtp-username")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			smtpPassword, err := cmd.Flags().GetString("smtp-password")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			params := reqBody.GetParams()
			params.SetSmtpData(ybaclient.SmtpData{
				EmailFrom:    util.GetStringPointer(emailFrom),
				SmtpServer:   util.GetStringPointer(server),
				SmtpPort:     util.GetInt32Pointer(int32(port)),
				UseSSL:       util.GetBoolPointer(useSSL),
				UseTLS:       util.GetBoolPointer(useTLS),
				SmtpUsername: util.GetStringPointer(smtpUsername),
				SmtpPassword: util.GetStringPointer(smtpPassword),
			})
			reqBody.SetParams(params)
		}

		channelutil.CreateChannelUtil(authAPI, "Alert Channel: Email", name, reqBody)

	},
}

func init() {
	createEmailChannelAlertCmd.Flags().SortFlags = false

	createEmailChannelAlertCmd.Flags().Bool("use-default-rececipients", false,
		"[Optional] Use default recipients for alert channel. (default false)")
	createEmailChannelAlertCmd.Flags().StringArray("recipients", []string{},
		fmt.Sprintf(
			"[Optional] Recipients for alert channel. Can be provided as separate flags or "+
				"as comma-separated values. %s",
			formatter.Colorize(
				"Required when use-default-recipients is false",
				formatter.GreenColor,
			),
		),
	)
	createEmailChannelAlertCmd.Flags().Bool("use-default-smtp-settings", false,
		"[Optional] Use default SMTP settings for alert channel. "+
			"Values of smtp-server, smtp-port, email-from, smtp-username, smtp-password, "+
			"use-ssl, use-tls are used if false. (default false)")

	createEmailChannelAlertCmd.Flags().String("smtp-server", "",
		fmt.Sprintf(
			"[Optional] SMTP server for alert channel. %s",
			formatter.Colorize(
				"Required when use-default-smtp-settings is false",
				formatter.GreenColor,
			),
		),
	)
	createEmailChannelAlertCmd.Flags().Int("smtp-port", -1,
		fmt.Sprintf(
			"[Optional] SMTP port for alert channel. %s",
			formatter.Colorize(
				"Required when use-default-smtp-settings is false",
				formatter.GreenColor,
			),
		),
	)
	createEmailChannelAlertCmd.Flags().String("email-from", "",
		fmt.Sprintf(
			"[Optional] SMTP email 'from' address. %s",
			formatter.Colorize(
				"Required when use-default-smtp-settings is false",
				formatter.GreenColor,
			),
		),
	)
	createEmailChannelAlertCmd.Flags().String("smtp-username", "",
		"[Optional] SMTP username.")
	createEmailChannelAlertCmd.Flags().String("smtp-password", "",
		"[Optional] SMTP password.")
	createEmailChannelAlertCmd.MarkFlagsRequiredTogether("smtp-username", "smtp-password")

	createEmailChannelAlertCmd.Flags().Bool("use-ssl", false,
		"[Optional] Use SSL for SMTP connection. (default false)")
	createEmailChannelAlertCmd.Flags().Bool("use-tls", false,
		"[Optional] Use TLS for SMTP connection. (default false)")
}
