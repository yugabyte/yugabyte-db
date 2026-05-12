/*
* Copyright (c) YugabyteDB, Inc.
 */

package channelutil

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/channel"
)

// ListChannelUtil lists alert channels
func ListChannelUtil(cmd *cobra.Command, commandCall, channelType string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	callSite := "Alert Channel"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}
	name, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	r := ListAndFilterAlertChannels(authAPI, callSite, "List", name, channelType)

	channelCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  channel.NewAlertChannelFormat(viper.GetString("output")),
	}
	if len(r) < 1 {
		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Info("No alert channels found\n")
		} else {
			logrus.Info("[]\n")
		}
		return
	}
	channel.Write(channelCtx, r)

}

// DeleteChannelUtil deletes an alert channel
func DeleteChannelUtil(cmd *cobra.Command, commandCall, channelType string) {
	callSite := "Alert Channel"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	channelName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	alerts := ListAndFilterAlertChannels(
		authAPI,
		callSite,
		"Delete - List Alert Channels",
		channelName,
		channelType,
	)

	if len(alerts) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No alert channels with name: %s found\n", channelName),
				formatter.RedColor),
		)
	}

	alertUUID := alerts[0].GetUuid()

	alertRequest := authAPI.DeleteAlertChannel(alertUUID)

	rDelete, response, err := alertRequest.Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Delete")
	}

	if rDelete.GetSuccess() {
		logrus.Info(fmt.Sprintf("The alert channel %s (%s) has been deleted",
			formatter.Colorize(channelName, formatter.GreenColor), alertUUID))

	} else {
		logrus.Errorf(
			formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while deleting alert channel %s (%s)\n",
					formatter.Colorize(channelName, formatter.GreenColor), alertUUID),
				formatter.RedColor))
	}
}

// DescribeChannelUtil describes the alert channel
func DescribeChannelUtil(cmd *cobra.Command, commandCall, channelType string) {
	callSite := "Alert Channel"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	channelName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	r := ListAndFilterAlertChannels(authAPI, callSite, "Describe", channelName, channelType)

	if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
		fullChannelContext := *channel.NewFullAlertChannelContext()
		fullChannelContext.Output = os.Stdout
		fullChannelContext.Format = channel.NewFullAlertChannelFormat(viper.GetString("output"))
		fullChannelContext.SetFullAlertChannel(r[0])
		fullChannelContext.Write()
		return
	}

	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No alert channels with name: %s found\n", channelName),
				formatter.RedColor,
			))
	}

	channelCtx := formatter.Context{
		Command: "describe",
		Output:  os.Stdout,
		Format:  channel.NewAlertChannelFormat(viper.GetString("output")),
	}
	channel.Write(channelCtx, r)
}

// ValidateDeleteChannelUtil validates the delete alert channel command
func ValidateDeleteChannelUtil(cmd *cobra.Command) {
	viper.BindPFlag("force", cmd.Flags().Lookup("force"))
	name, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(name) == 0 {
		logrus.Fatal(
			formatter.Colorize(
				"No name specified to delete alert channel\n",
				formatter.RedColor))
	}
	err = util.ConfirmCommand(
		fmt.Sprintf("Are you sure you want to delete %s: %s", "alert channel", name),
		viper.GetBool("force"))
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
	}
}

// ValidateChannelUtil validates the describe alert channel command
func ValidateChannelUtil(cmd *cobra.Command, operation string) {
	channelNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(channelNameFlag) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No alert channel name found to +"+operation+"\n",
				formatter.RedColor,
			),
		)
	}
}

// ListAndFilterAlertChannels lists and filters alert channels
func ListAndFilterAlertChannels(
	authAPI *ybaAuthClient.AuthAPIClient, callSite, operation, channelName, channelType string,
) []util.AlertChannel {
	alerts, err := authAPI.ListAlertChannelsRest(callSite, operation)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	if !util.IsEmptyString(channelName) {
		rNameList := make([]util.AlertChannel, 0)
		for _, alertChannel := range alerts {
			if strings.EqualFold(alertChannel.GetName(), channelName) {
				rNameList = append(rNameList, alertChannel)
			}
		}
		alerts = rNameList
	}

	if !util.IsEmptyString(channelType) {
		rTypeList := make([]util.AlertChannel, 0)
		for _, alertChannel := range alerts {
			params := alertChannel.GetParams()
			if strings.EqualFold(params.GetChannelType(), channelType) {
				rTypeList = append(rTypeList, alertChannel)
			}
		}
		alerts = rTypeList
	}

	return alerts
}

// CreateChannelUtil creates an alert channel
func CreateChannelUtil(
	authAPI *ybaAuthClient.AuthAPIClient,
	callSite, name string,
	reqBody util.AlertChannelFormData,
) {
	r, err := authAPI.CreateAlertChannelRest(reqBody, callSite)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	alertChannelUUID := r.GetUuid()

	if util.IsEmptyString(alertChannelUUID) {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"An error occurred while adding alert channel %s\n",
				name),
			formatter.RedColor))
	}

	logrus.Infof(
		"Successfully added alert channel %s (%s)\n",
		formatter.Colorize(name, formatter.GreenColor), alertChannelUUID)

	alerts := make([]util.AlertChannel, 0)
	alerts = append(alerts, r)
	alertCtx := formatter.Context{
		Command: "create",
		Output:  os.Stdout,
		Format:  channel.NewAlertChannelFormat(viper.GetString("output")),
	}

	channel.Write(alertCtx, alerts)
}

// UpdateChannelUtil updates an alert channel
func UpdateChannelUtil(
	authAPI *ybaAuthClient.AuthAPIClient,
	callSite, name, uuid string,
	reqBody util.AlertChannelFormData,
) {
	r, err := authAPI.UpdateAlertChannelRest(uuid, callSite, reqBody)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	alertChannelUUID := r.GetUuid()

	if util.IsEmptyString(alertChannelUUID) {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"An error occurred while updating alert channel %s\n",
				name),
			formatter.RedColor))
	}

	logrus.Infof(
		"Successfully updated alert channel %s (%s)\n",
		formatter.Colorize(name, formatter.GreenColor), alertChannelUUID)

	alerts := make([]util.AlertChannel, 0)
	alerts = append(alerts, r)
	alertCtx := formatter.Context{
		Command: "update",
		Output:  os.Stdout,
		Format:  channel.NewAlertChannelFormat(viper.GetString("output")),
	}

	channel.Write(alertCtx, alerts)
}
