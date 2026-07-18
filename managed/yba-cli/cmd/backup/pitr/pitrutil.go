/*
 * Copyright (c) YugabyteDB, Inc.
 */

package pitr

import (
	"fmt"
	"os"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/pitr"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// ListPITRUtil executes the list pitr command
func ListPITRUtil(cmd *cobra.Command, universeName string) {
	authAPI, universe, err := universeutil.Validations(cmd, util.PITROperation)
	if err != nil {
		logrus.Fatal(
			formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	universeUUID := universe.GetUniverseUUID()
	ListPITRForUniverse(authAPI, universeUUID, universeName)
}

// CreatePITRUtil executes the create pitr command
func CreatePITRUtil(cmd *cobra.Command, universeName string, keyspaceName string,
	tableType string, retentionPeriod int64, scheduleInterval int64) {
	authAPI, universe, err := universeutil.Validations(cmd, util.PITROperation)
	if err != nil {
		logrus.Fatal(
			formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	universeUUID := universe.GetUniverseUUID()
	requestBody := ybaclient.CreatePitrConfigParams{
		RetentionPeriodInSeconds: util.GetInt64Pointer(retentionPeriod),
		IntervalInSeconds:        util.GetInt64Pointer(scheduleInterval),
	}
	task, response, err := authAPI.CreatePITRConfig(universeUUID, tableType, keyspaceName).
		PitrConfig(requestBody).
		Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "PITR", "Create")
		logrus.Fatal(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	util.CheckTaskAfterCreation(task)

	taskUUID := task.GetTaskUUID()
	msg := fmt.Sprintf(
		"Creating PITR configuration for keyspace %s on universe %s (%s).",
		formatter.Colorize(keyspaceName, formatter.GreenColor),
		universeName,
		universeUUID,
	)
	logrus.Info(msg + "\n")
	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf(
				"\nWaiting for creation of PITR config for keyspace %s  on universe %s (%s) to be completed\n",
				formatter.Colorize(keyspaceName, formatter.GreenColor),
				universeName,
				universeUUID,
			))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The PITR configuration for universe %s (%s) has been created.\n",
			universeName, universeUUID)
		ListPITRForUniverse(authAPI, universeUUID, universeName)
	} else {
		taskCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*task})
	}
}

// DeletePITRUtil executes the delete pitr command
func DeletePITRUtil(cmd *cobra.Command, universeName string, pitrUUID string) {
	authAPI, universe, err := universeutil.Validations(cmd, util.PITROperation)
	if err != nil {
		logrus.Fatal(
			formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	universeUUID := universe.GetUniverseUUID()
	task, response, err := authAPI.DeletePITRConfig(universeUUID, pitrUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "PITR", "Delete")
		logrus.Fatal(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	util.CheckTaskAfterCreation(task)
	taskUUID := task.GetTaskUUID()
	msg := fmt.Sprintf(
		"Deleting PITR configuration %s for universe %s (%s)...",
		pitrUUID,
		universeName,
		universeUUID,
	)
	logrus.Info(msg + "\n")
	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(
				fmt.Sprintf(
					"Waiting for deletion of PITR configuration %s on universe %s (%s) to complete\n",
					pitrUUID,
					universeName,
					universeUUID,
				))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The PITR configuration for universe %s (%s) has been deleted.\n",
			universeName, universeUUID)
	} else {
		taskCtx := formatter.Context{
			Command: "delete",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*task})
	}
}

// EditPITRUtil executes the edit pitr command
func EditPITRUtil(
	cmd *cobra.Command,
	universeName string,
	pitrUUID string,
	retentionPeriod int64,
) {
	authAPI, universe, err := universeutil.Validations(cmd, util.PITROperation)
	if err != nil {
		logrus.Fatal(
			formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	universeUUID := universe.GetUniverseUUID()
	requestBody := ybaclient.UpdatePitrConfigParams{
		RetentionPeriodInSeconds: util.GetInt64Pointer(retentionPeriod),
	}
	task, response, err := authAPI.UpdatePITRConfig(universeUUID, pitrUUID).
		PitrConfig(requestBody).
		Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "PITR", "Edit")
		logrus.Fatal(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	util.CheckTaskAfterCreation(task)
	taskUUID := task.GetTaskUUID()
	msg := fmt.Sprintf("Editing PITR configuration %s for universe %s (%s)...",
		pitrUUID, universeName, universeUUID)
	logrus.Info(msg + "\n")
	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(
				fmt.Sprintf(
					"Waiting for PITR configuration %s update to complete on universe %s (%s)...",
					pitrUUID,
					universeName,
					universeUUID,
				),
			)
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The PITR configuration %s for universe %s (%s) has been edited.\n",
			pitrUUID, universeName, universeUUID)
		ListPITRForUniverse(authAPI, universeUUID, universeName)
	} else {
		taskCtx := formatter.Context{
			Command: "edit",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*task})
	}
}

// RecoverToPointInTimeUtil executes the recover pitr command
func RecoverToPointInTimeUtil(
	cmd *cobra.Command,
	universeName string,
	pitrUUID string,
	timestamp int64,
) {
	authAPI, universe, err := universeutil.Validations(cmd, util.PITROperation)
	if err != nil {
		logrus.Fatal(
			formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	universeUUID := universe.GetUniverseUUID()
	ValidateTimestamp(timestamp, universeUUID, pitrUUID)
	requestBody := ybaclient.RestoreSnapshotScheduleParams{
		RestoreTimeInMillis: util.GetInt64Pointer(timestamp),
		PitrConfigUUID:      util.GetStringPointer(pitrUUID),
	}
	task, response, err := authAPI.PerformPITR(universeUUID).PerformPitr(requestBody).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "PITR", "Recover")
		logrus.Fatal(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	util.CheckTaskAfterCreation(task)
	taskUUID := task.GetTaskUUID()
	msg := fmt.Sprintf("PITR recovery in progress for universe %s (%s).",
		universeName, universeUUID)
	logrus.Info(msg + "\n")
	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf(
				"\nWaiting for PITR recovery on universe %s (%s) to be completed\n",
				universeName, universeUUID))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The PITR recovery for universe %s (%s) has been completed.\n",
			universeName, universeUUID)
	} else {
		taskCtx := formatter.Context{
			Command: "recover",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*task})
	}
}

// ListPITRForUniverse lists the PITR configurations for a universe
func ListPITRForUniverse(
	authAPI *ybaAuthClient.AuthAPIClient,
	universeUUID string,
	universeName string,
) {
	r, response, err := authAPI.ListPITRConfig(universeUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"PITR",
			"List")
		logrus.Fatal(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	pitrCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  pitr.NewPITRFormat(viper.GetString("output")),
	}
	if len(r) < 1 {
		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Info(
				fmt.Sprintf(
					"No pitr config found for the universe %s (%s)\n",
					universeName,
					universeUUID,
				),
			)
		} else {
			logrus.Info("[]\n")
		}
		return
	}
	pitr.Write(pitrCtx, r)
}

// ValidateTimestamp validates the timestamp for PITR recovery
func ValidateTimestamp(timestamp int64, universeUUID string, pitrUUID string) {
	if timestamp <= 0 {
		logrus.Fatal(formatter.Colorize("Timestamp should be greater than 0\n", formatter.RedColor))
	}
	minTimestamp, maxTimestamp := GetMinAndMaxRecoverTimeInMillis(universeUUID, pitrUUID)
	if timestamp < minTimestamp || timestamp > maxTimestamp {
		msg := fmt.Sprintf("Timestamp should be between %d (%v) and %d (%v) \n",
			minTimestamp,
			time.Unix(0, minTimestamp*int64(time.Millisecond)),
			maxTimestamp,
			time.Unix(0, maxTimestamp*int64(time.Millisecond)))
		logrus.Fatal(formatter.Colorize(msg, formatter.RedColor))
	}
}

// GetMinAndMaxRecoverTimeInMillis returns the possible min and max recover time in millis
func GetMinAndMaxRecoverTimeInMillis(universeUUID string, pitrUUID string) (int64, int64) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	r, response, err := authAPI.ListPITRConfig(universeUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"PITR",
			"Get Retention Period")
		logrus.Fatal(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	for _, pitr := range r {
		if pitr.GetUuid() == pitrUUID {
			return pitr.GetMinRecoverTimeInMillis(), pitr.GetMaxRecoverTimeInMillis()
		}
	}
	logrus.Fatal(
		formatter.Colorize(
			fmt.Sprintf("PITR config - %s not found\n", pitrUUID),
			formatter.RedColor,
		),
	)
	return 0, 0
}
