/*
 * Copyright (c) YugabyteDB, Inc.
 */

package email

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateEmailChannelAlertCmd represents the ear command
var updateEmailChannelAlertCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere Email alert channel",
	Long:    "Update a Email alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel email update --name <channel-name> \
   --new-name <new-channel-name> --use-default-recipients --recipients <recipients>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "update")
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		callSite := "Alert Channel: Email"

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		alerts := channelutil.ListAndFilterAlertChannels(
			authAPI,
			callSite,
			"Update - List Alert Channels",
			name,
			util.EmailAlertChannelType,
		)

		if len(alerts) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					"No email alert channels with name: "+name+" found\n",
					formatter.RedColor,
				),
			)
		}

		alert := alerts[0]
		params := alert.GetParams()
		smtpData := params.GetSmtpData()

		hasUpdates := false

		newName, err := cmd.Flags().GetString("new-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(newName) {
			hasUpdates = true
			logrus.Debug("Updating alert channel name\n")
			alert.SetName(newName)
		}

		if cmd.Flags().Changed("use-default-rececipients") {
			defaultRecipients, err := cmd.Flags().GetBool("use-default-rececipients")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			hasUpdates = true
			logrus.Debug("Updating alert channel default recipients\n")
			params.SetDefaultRecipients(defaultRecipients)
		}

		if !params.GetDefaultRecipients() {
			if cmd.Flags().Changed("recipients") {
				recipients, err := cmd.Flags().GetStringArray("recipients")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				if len(recipients) == 0 {
					logrus.Fatalf(
						formatter.Colorize(
							"Recipients are required when use-default-recipients is false\n",
							formatter.RedColor,
						),
					)
				}
				hasUpdates = true
				logrus.Debug("Updating alert channel recipients\n")
				params.SetRecipients(recipients)
			}
		} else {
			params.SetRecipients([]string{})
		}

		if cmd.Flags().Changed("use-default-smtp-settings") {
			defaultSMTPSettings, err := cmd.Flags().GetBool("use-default-smtp-settings")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			hasUpdates = true
			logrus.Debug("Updating alert channel default SMTP settings\n")
			params.SetDefaultSmtpSettings(defaultSMTPSettings)
		}

		if !params.GetDefaultSmtpSettings() {
			emailFrom, err := cmd.Flags().GetString("email-from")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			hasUpdates = true
			logrus.Debug("Updating alert channel email from\n")
			smtpData.SetEmailFrom(emailFrom)

			if smtpData.GetEmailFrom() == "" {
				logrus.Fatalf(
					formatter.Colorize(
						"Email from is required when use-default-smtp-settings is false\n",
						formatter.RedColor,
					),
				)
			}

			server, err := cmd.Flags().GetString("smtp-server")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			hasUpdates = true
			logrus.Debug("Updating alert channel SMTP server\n")
			smtpData.SetSmtpServer(server)

			if smtpData.GetSmtpServer() == "" {
				logrus.Fatalf(
					formatter.Colorize(
						"SMTP server is required when use-default-smtp-settings is false\n",
						formatter.RedColor,
					),
				)
			}

			if cmd.Flags().Changed("smtp-port") {
				port, err := cmd.Flags().GetInt("smtp-port")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				hasUpdates = true
				logrus.Debug("Updating alert channel SMTP port\n")
				smtpData.SetSmtpPort(int32(port))
			}

			if cmd.Flags().Changed("use-ssl") {
				useSSL, err := cmd.Flags().GetBool("use-ssl")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				hasUpdates = true
				logrus.Debug("Updating alert channel use SSL\n")
				smtpData.SetUseSSL(useSSL)
			}

			if cmd.Flags().Changed("use-tls") {

				useTLS, err := cmd.Flags().GetBool("use-tls")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				hasUpdates = true
				logrus.Debug("Updating alert channel use TLS\n")
				smtpData.SetUseTLS(useTLS)
			}

			smtpUsername, err := cmd.Flags().GetString("smtp-username")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			hasUpdates = true
			logrus.Debug("Updating alert channel SMTP username\n")
			smtpData.SetSmtpUsername(smtpUsername)

			smtpPassword, err := cmd.Flags().GetString("smtp-password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			hasUpdates = true
			logrus.Debug("Updating alert channel SMTP password\n")
			smtpData.SetSmtpPassword(smtpPassword)
			params.SetSmtpData(smtpData)
		} else {
			params.SmtpData = nil
		}

		alert.SetParams(params)

		requestBody := util.AlertChannelFormData{
			Name:             alert.GetName(),
			Params:           alert.GetParams(),
			AlertChannelUUID: alert.GetUuid(),
		}

		if hasUpdates {
			channelutil.UpdateChannelUtil(
				authAPI,
				util.EmailAlertChannelType,
				name,
				alert.GetUuid(),
				requestBody)
			return
		}
		logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))
	},
}

func init() {
	updateEmailChannelAlertCmd.Flags().SortFlags = false

	updateEmailChannelAlertCmd.Flags().String("new-name", "",
		"[Optional] Update name of the alert channel.")
	updateEmailChannelAlertCmd.Flags().Bool("use-default-rececipients", false,
		"[Optional] Update use default recipients for alert channel.")
	updateEmailChannelAlertCmd.Flags().StringArray("recipients", []string{},
		"[Optional] Update recipients for alert channel.",
	)
	updateEmailChannelAlertCmd.Flags().Bool("use-default-smtp-settings", false,
		"[Optional] Update use default SMTP settings for alert channel.")

	updateEmailChannelAlertCmd.Flags().String("smtp-server", "",
		"[Optional] Update SMTP server for alert channel.",
	)
	updateEmailChannelAlertCmd.Flags().Int("smtp-port", -1,
		"[Optional] Update SMTP port for alert channel.",
	)
	updateEmailChannelAlertCmd.Flags().String("email-from", "",
		"[Optional] Update email from for alert channel.",
	)
	updateEmailChannelAlertCmd.Flags().String("smtp-username", "",
		"[Optional] Update SMTP username.")
	updateEmailChannelAlertCmd.Flags().String("smtp-password", "",
		"[Optional] Update SMTP password.")
	updateEmailChannelAlertCmd.MarkFlagsRequiredTogether("smtp-username", "smtp-password")

	updateEmailChannelAlertCmd.Flags().Bool("use-ssl", false,
		"[Optional] Update use SSL for SMTP connection.")
	updateEmailChannelAlertCmd.Flags().Bool("use-tls", false,
		"[Optional] Update use TLS for SMTP connection.")
}
