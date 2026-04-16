/*
* Copyright (c) YugabyteDB, Inc.
 */

package alert

import (
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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/customer"
)

// controlAlertCmd represents the control customer alert controls
var controlAlertCmd = &cobra.Command{
	Use:     "control",
	Aliases: []string{"health"},
	Short:   "Manage default alert controls",
	Long:    "Manage default alert controls",
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		customerDetails, response, err := authAPI.CustomerDetail().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert", "Control - Get Customer")
		}

		if customerDetails.GetUuid() == "" {
			logrus.Fatalf(formatter.Colorize("Customer not found\n", formatter.RedColor))
		}

		callhomeLevel, err := cmd.Flags().GetString("callhome-level")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(callhomeLevel) {
			callhomeLevel = customerDetails.GetCallhomeLevel()
		}
		callhomeLevel = strings.ToUpper(callhomeLevel)

		alertingData := customerDetails.GetAlertingData()
		if cmd.Flags().Changed("health-check-interval") {
			healthCheckInterval, err := cmd.Flags().GetInt64("health-check-interval")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if healthCheckInterval < 5 {
				logrus.Fatalf(
					formatter.Colorize(
						"Health check interval should be greater than 5 minutes\n",
						formatter.RedColor,
					),
				)
			}
			alertingData.SetCheckIntervalMs(healthCheckInterval * 60 * 1000)

		}
		if cmd.Flags().Changed("active-alert-notification-interval") {
			activeAlertNotificationInterval, err := cmd.Flags().
				GetInt64("active-alert-notification-interval")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			alertingData.SetActiveAlertNotificationIntervalMs(
				activeAlertNotificationInterval * 60 * 1000,
			)
		}

		smtpData := customerDetails.GetSmtpData()
		emailAlerts, err := cmd.Flags().GetString("email-alerts")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		switch strings.ToUpper(emailAlerts) {
		case util.EnableOpType:
			defaultRecipients, err := cmd.Flags().GetStringArray("default-recipients")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(defaultRecipients) == 0 {
				logrus.Fatalf(
					formatter.Colorize(
						"Default recipients are required when email-alerts is enabled\n",
						formatter.RedColor,
					),
				)
			}
			alertingData.SetAlertingEmail(strings.Join(defaultRecipients, ","))

			sendEmailsToYBTeam, err := cmd.Flags().GetBool("send-emails-to-yb-team")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			alertingData.SetSendAlertsToYb(sendEmailsToYBTeam)

			emailFrom, err := cmd.Flags().GetString("email-from")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(emailFrom) {
				logrus.Fatalf(
					formatter.Colorize(
						"Email from is required when email-alerts is enabled\n",
						formatter.RedColor,
					),
				)
			}
			smtpData.SetEmailFrom(emailFrom)

			smtpServer, err := cmd.Flags().GetString("smtp-server")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(smtpServer) {
				logrus.Fatalf(
					formatter.Colorize(
						"SMTP server is required when email-alerts is enabled\n",
						formatter.RedColor,
					),
				)
			}
			smtpData.SetSmtpServer(smtpServer)

			if cmd.Flags().Changed("smtp-port") {
				smtpPort, err := cmd.Flags().GetInt("smtp-port")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				smtpData.SetSmtpPort(int32(smtpPort))
			} else {
				logrus.Fatalf(formatter.Colorize("SMTP port is required when email-alerts is enabled\n", formatter.RedColor))
			}

			smtpUsername, err := cmd.Flags().GetString("smtp-username")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			smtpData.SmtpUsername = util.GetStringPointer(smtpUsername)

			smtpPassword, err := cmd.Flags().GetString("smtp-password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			smtpData.SmtpPassword = util.GetStringPointer(smtpPassword)

			useSSL, err := cmd.Flags().GetBool("use-ssl")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			smtpData.SetUseSSL(useSSL)

			useTLS, err := cmd.Flags().GetBool("use-tls")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			smtpData.SetUseTLS(useTLS)

			if cmd.Flags().Changed("health-check-email-interval") {
				healthCheckEmailInterval, err := cmd.Flags().GetInt64("health-check-email-interval")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				alertingData.SetStatusUpdateIntervalMs(healthCheckEmailInterval * 60 * 1000)
			}

			includeOnlyErrorsInEmail, err := cmd.Flags().GetBool("include-only-errors-in-email")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			alertingData.SetReportOnlyErrors(includeOnlyErrorsInEmail)

		case util.DisableOpType:
			smtpData = ybaclient.SmtpData{}
			alertingData.SetAlertingEmail("")
			alertingData.SetSendAlertsToYb(false)
			alertingData.SetStatusUpdateIntervalMs(0)
			alertingData.SetReportOnlyErrors(false)

		default:
			logrus.Fatalf(
				formatter.Colorize(
					"Invalid email-alerts value. Allowed values: enable, disable\n",
					formatter.RedColor,
				),
			)
		}

		req := ybaclient.CustomerAlertData{
			CallhomeLevel: callhomeLevel,
			AlertingData:  alertingData,
			SmtpData:      smtpData,
		}

		_, response, err = authAPI.UpdateCustomer().Customer(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert", "Control")
		}

		customerDetails, response, err = authAPI.CustomerDetail().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert", "Control - Get Customer")
		}

		r := util.CheckAndAppend(
			make([]ybaclient.CustomerDetailsData, 0),
			customerDetails,
			"No customer details found",
		)

		customerCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  customer.NewCustomerFormat(viper.GetString("output")),
		}

		customer.Write(customerCtx, r)
	},
}

func init() {
	controlAlertCmd.PersistentFlags().SortFlags = false
	controlAlertCmd.Flags().SortFlags = false

	controlAlertCmd.Flags().
		String("callhome-level", "", "[Optional] Manage callhome level. Allowed values: none, low, medium, high.")

	controlAlertCmd.Flags().
		Int64("health-check-interval", 5,
			"[Optional] Health check intervals in minutes. Value below 5 minutes is not allowed.")

	controlAlertCmd.Flags().
		Int64("active-alert-notification-interval", 0,
			"[Optional] Period which is used to send active alert notifications in minutes.")

	controlAlertCmd.Flags().
		String("email-alerts", "", "[Optional] Manage email notifications. Allowed values: enable, disable.")

	controlAlertCmd.Flags().StringArray("default-recipients", []string{},
		fmt.Sprintf("[Optional] Edit default recipients for email notifications. "+
			"Can be provided as separate flags or as comma-separated values. %s.",
			formatter.Colorize("Required when email-alerts is enabled", formatter.GreenColor)))

	controlAlertCmd.Flags().
		Bool("send-emails-to-yb-team", false, "[Optional] Send emails to YugabyteDB team. (default false)")

	controlAlertCmd.Flags().
		String("email-from", "",
			fmt.Sprintf("[Optional] Email address to send alerts from. %s",
				formatter.Colorize("Required when email-alerts is enabled", formatter.GreenColor)),
		)

	controlAlertCmd.Flags().
		String("smtp-server", "",
			fmt.Sprintf("[Optional] SMTP server address. %s",
				formatter.Colorize("Required when email-alerts is enabled", formatter.GreenColor)),
		)

	controlAlertCmd.Flags().
		Int("smtp-port", -1,
			fmt.Sprintf("[Optional] SMTP server port. %s",
				formatter.Colorize("Required when email-alerts is enabled", formatter.GreenColor)),
		)

	controlAlertCmd.Flags().String("smtp-username", "", "[Optional] SMTP server username.")

	controlAlertCmd.Flags().String("smtp-password", "", "[Optional] SMTP server password.")

	controlAlertCmd.Flags().
		Bool("use-ssl", false, "[Optional] Use SSL for SMTP connection. (default false)")

	controlAlertCmd.Flags().
		Bool("use-tls", false, "[Optional] Use TLS for SMTP connection. (default false)")

	controlAlertCmd.Flags().
		Int64("health-check-email-interval", 0, "[Optional] Period between health check email notifications in minutes.")

	controlAlertCmd.Flags().
		Bool("include-only-errors-in-email", false, "[Optional] Include only errors in email notifications. (default false)")

}
