/*
 * Copyright (c) YugaByte, Inc.
 */

package upgrade

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// upgradeGflagsCmd represents the universe upgrade gflags command
var upgradeGflagsCmd = &cobra.Command{
	Use:   "gflags",
	Short: "Gflags upgrade for a YugabyteDB Anywhere Universe",
	Long:  "Gflags upgrade for a YugabyteDB Anywhere Universe",
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to upgrade\n", formatter.RedColor))
		}

		// Validations before gflags upgrade operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := Validations(cmd, util.UpgradeOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}

		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to upgrade Gflags %s: %s",
				util.UniverseType, universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := Validations(cmd, util.UpgradeOperation)
		if err != nil {
			logrus.Fatalf(
				formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()
		universeDetails := universe.GetUniverseDetails()
		clusters := universeDetails.GetClusters()
		if len(clusters) < 1 {
			logrus.Fatalln(formatter.Colorize(
				"No clusters found in universe "+universeName+" ("+universeUUID+")\n",
				formatter.RedColor,
			))
		}

		primaryUserIntent := clusters[0].GetUserIntent()
		primarySpecificGflags := primaryUserIntent.GetSpecificGFlags()
		primaryPerProccessFlags := primarySpecificGflags.GetPerProcessFlags()
		primaryGflagsValue := primaryPerProccessFlags.GetValue()
		oldMasterGflags := primaryGflagsValue[util.MasterServerType]
		oldTserverGflags := primaryGflagsValue[util.TserverServerType]

		masterGflags, err := editMasterGflags(cmd, oldMasterGflags)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tserverGflags, err := editTserverGflags(cmd, "primary", oldTserverGflags)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		primaryGflagsValue[util.TserverServerType] = tserverGflags
		primaryGflagsValue[util.MasterServerType] = masterGflags
		primaryPerProccessFlags.SetValue(primaryGflagsValue)
		primarySpecificGflags.SetPerProcessFlags(primaryPerProccessFlags)
		primaryUserIntent.SetSpecificGFlags(primarySpecificGflags)
		clusters[0].SetUserIntent(primaryUserIntent)

		if len(clusters) > 1 {
			inheritFromPrimary, err := cmd.Flags().GetBool("inherit-from-primary")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			rrUserIntent := clusters[1].GetUserIntent()
			rrSpecificGflags := rrUserIntent.GetSpecificGFlags()
			rrPerProccessFlags := rrSpecificGflags.GetPerProcessFlags()
			rrGflagsValue := rrPerProccessFlags.GetValue()
			clusterInheritFromPrimaryValue := rrSpecificGflags.GetInheritFromPrimary()
			if !inheritFromPrimary {
				rrSpecificGflags.SetInheritFromPrimary(false)
				var oldRRTserverGflags map[string]string
				if !clusterInheritFromPrimaryValue {
					oldRRTserverGflags = rrGflagsValue[util.TserverServerType]
				} else {
					oldRRTserverGflags = oldMasterGflags
					rrGflagsValue = make(map[string]map[string]string, 0)
					rrGflagsValue[util.TserverServerType] = oldMasterGflags
				}
				rrTserverGflags, err := editTserverGflags(cmd, "rr", oldRRTserverGflags)
				if err != nil {
					logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				rrGflagsValue[util.TserverServerType] = rrTserverGflags
				rrPerProccessFlags.SetValue(rrGflagsValue)
				rrSpecificGflags.SetPerProcessFlags(rrPerProccessFlags)
				rrSpecificGflags.SetInheritFromPrimary(false)
			} else {
				rrSpecificGflags.SetInheritFromPrimary(true)
			}
			rrUserIntent.SetSpecificGFlags(rrSpecificGflags)
			clusters[1].SetUserIntent(rrUserIntent)
		}

		upgradeOption, err := cmd.Flags().GetString("upgrade-option")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		masterDelay, err := cmd.Flags().GetInt32("delay-between-master-servers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tserverDelay, err := cmd.Flags().GetInt32("delay-between-tservers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.GFlagsUpgradeParams{
			Clusters:                       clusters,
			UpgradeOption:                  upgradeOption,
			SleepAfterTServerRestartMillis: tserverDelay,
			SleepAfterMasterRestartMillis:  masterDelay,
		}

		rUpgrade, response, err := authAPI.UpgradeGFlags(universeUUID).
			GflagsUpgradeParams(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Upgrade GFlags")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		taskUUID := rUpgrade.GetTaskUUID()
		logrus.Info(
			fmt.Sprintf("Upgrading universe %s (%s) gflags\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
			))

		WaitForUpgradeUniverseTask(authAPI, universeName, universeUUID, taskUUID)
	},
}

func init() {
	upgradeGflagsCmd.Flags().SortFlags = false

	upgradeGflagsCmd.Flags().String("add-master-gflags", "",
		"[Optional] Add Master GFlags to existing list. "+
			"Provide comma-separated key-value pairs for the primary "+
			"cluster in the following format: "+
			"\"--add-master-gflags master-gflag-key-1=master-gflag-value-1,"+
			"master-gflag-key-2=master-gflag-key2\".")
	upgradeGflagsCmd.Flags().String("edit-master-gflags", "",
		"[Optional] Edit Master GFlags in existing list. "+
			"Provide comma-separated key-value pairs for the primary "+
			"cluster in the following format: "+
			"\"--edit-master-gflags master-gflag-key-1=master-gflag-value-1,"+
			"master-gflag-key-2=master-gflag-key2\".")
	upgradeGflagsCmd.Flags().String("remove-master-gflags", "",
		"[Optional] Remove Master GFlags from existing list. "+
			"Provide comma-separated values for the primary "+
			"cluster in the following format: "+
			"\"--remove-master-gflags master-gflag-key-1,master-gflag-key-2\".")

	upgradeGflagsCmd.Flags().Bool("inherit-from-primary", true,
		"[Optional] Apply the TServer GFlags changes of primary cluster to read replica cluster.")

	upgradeGflagsCmd.Flags().String("add-primary-tserver-gflags", "",
		"[Optional] Add TServer GFlags to primary cluster. Provide comma-separated key-value"+
			" pairs in the following format: "+
			"\"--add-primary-tserver-gflags tserver-gflag-key-1-for-primary-cluster="+
			"tserver-gflag-value-1,tserver-gflag-key-2-for-primary-cluster=tserver-gflag-key2\"."+
			" If no-of-clusters = 2 "+
			"and inherit-from-primary is set to true, "+
			"these gflags are copied to the read replica cluster.")
	upgradeGflagsCmd.Flags().String("edit-primary-tserver-gflags", "",
		"[Optional] Edit TServer GFlags in primary cluster. Provide comma-separated key-value"+
			" pairs in the following format: "+
			"\"--edit-primary-tserver-gflags tserver-gflag-key-1-for-primary-cluster="+
			"tserver-gflag-value-1,tserver-gflag-key-2-for-primary-cluster=tserver-gflag-key2\"."+
			" If no-of-clusters = 2 "+
			"and inherit-from-primary is set to true, these gflag "+
			"values are copied to the read replica cluster.")
	upgradeGflagsCmd.Flags().String("remove-primary-tserver-gflags", "",
		"[Optional] Remove TServer GFlags from primary cluster. Provide comma-separated keys"+
			" in the following format: "+
			"\"--remove-primary-tserver-gflags tserver-gflag-key-1-for-primary-cluster"+
			",tserver-gflag-key-2-for-primary-cluster\"."+
			" If no-of-clusters = 2 "+
			"and inherit-from-primary is set to true, these gflag "+
			"keys are removed from the read replica cluster.")

	upgradeGflagsCmd.Flags().String("add-rr-tserver-gflags", "",
		"[Optional] Add TServer GFlags to Read Replica cluster. Provide comma-separated key-value"+
			" pairs in the following format: "+
			"\"--add-rr-tserver-gflags tserver-gflag-key-1-for-rr-cluster="+
			"tserver-gflag-value-1,tserver-gflag-key-2-for-rr-cluster=tserver-gflag-key2\"."+
			" Ignored if inherit-from-primary is set to true.")
	upgradeGflagsCmd.Flags().String("edit-rr-tserver-gflags", "",
		"[Optional] Edit TServer GFlags in Read replica cluster. Provide comma-separated key-value"+
			" pairs in the following format: "+
			"\"--edit-rr-tserver-gflags tserver-gflag-key-1-for-rr-cluster="+
			"tserver-gflag-value-1,tserver-gflag-key-2-for-rr-cluster=tserver-gflag-key2\"."+
			" Ignored if inherit-from-primary is set to true.")
	upgradeGflagsCmd.Flags().String("remove-rr-tserver-gflags", "",
		"[Optional] Remove TServer GFlags from Read replica cluster. Provide comma-separated keys"+
			" in the following format: "+
			"\"--remove-rr-tserver-gflags tserver-gflag-key-1-for-rr-cluster"+
			",tserver-gflag-key-2-for-rr-cluster\"."+
			" Ignored if inherit-from-primary is set to true.")

	upgradeGflagsCmd.Flags().String("upgrade-option", "Rolling",
		"[Optional] Upgrade Options, defaults to Rolling. "+
			"Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime), Non-Restart")
	upgradeGflagsCmd.Flags().Int32("delay-between-master-servers",
		18000, "[Optional] Upgrade delay between Master servers (in miliseconds).")
	upgradeGflagsCmd.Flags().Int32("delay-between-tservers",
		18000, "[Optional] Upgrade delay between Tservers (in miliseconds).")

}

func editMasterGflags(
	cmd *cobra.Command,
	masterGflags map[string]string) (map[string]string, error) {
	addMasterGFlagsString, err := cmd.Flags().GetString("add-master-gflags")
	if err != nil {
		return nil, err
	}
	addMasterGFlags := FetchMasterGFlags(addMasterGFlagsString)

	editMasterGFlagsString, err := cmd.Flags().GetString("edit-master-gflags")
	if err != nil {
		return nil, err
	}
	editMasterGFlags := FetchMasterGFlags(editMasterGFlagsString)

	removeMasterGFlagsString, err := cmd.Flags().GetString("remove-master-gflags")
	if err != nil {
		return nil, err
	}

	for key, value := range addMasterGFlags {
		if _, exists := masterGflags[key]; exists {
			logrus.Info(
				fmt.Sprintf("%s already exists in master gflags, ignoring\n", key))
		} else {
			masterGflags[key] = value
		}
	}

	for key, value := range editMasterGFlags {
		if _, exists := masterGflags[key]; !exists {
			logrus.Info(
				fmt.Sprintf("%s does not exist in master gflags to edit, ignoring\n", key))
		} else {
			masterGflags[key] = value
		}
	}

	if len(strings.TrimSpace(removeMasterGFlagsString)) != 0 {
		for _, key := range strings.Split(removeMasterGFlagsString, ",") {
			if _, exists := masterGflags[key]; !exists {
				logrus.Info(
					fmt.Sprintf("%s does not exist in master gflags to remove, ignoring\n", key))
			} else {
				delete(masterGflags, key)
			}
		}
	}
	return masterGflags, nil
}

func editTserverGflags(
	cmd *cobra.Command,
	cluster string,
	tserverGflags map[string]string,
) (map[string]string, error) {
	addFlag := fmt.Sprintf("add-%s-tserver-gflags", cluster)
	editFlag := fmt.Sprintf("edit-%s-tserver-gflags", cluster)
	removeFlag := fmt.Sprintf("remove-%s-tserver-gflags", cluster)

	addGFlagsString, err := cmd.Flags().GetString(addFlag)
	if err != nil {
		return nil, err
	}
	addGFlags := FetchTServerGFlags([]string{addGFlagsString}, 1)

	editGFlagsString, err := cmd.Flags().GetString(editFlag)
	if err != nil {
		return nil, err
	}
	editGFlags := FetchTServerGFlags([]string{editGFlagsString}, 1)

	removeGFlagsString, err := cmd.Flags().GetString(removeFlag)
	if err != nil {
		return nil, err
	}

	if len(addGFlags) > 0 {
		for key, value := range addGFlags[0] {
			if _, exists := tserverGflags[key]; exists {
				logrus.Info(
					fmt.Sprintf("%s already exists in %s tserver gflags, ignoring\n", key, cluster))
			} else {
				tserverGflags[key] = value
			}
		}
	}
	if len(editFlag) > 0 {
		for key, value := range editGFlags[0] {
			if _, exists := tserverGflags[key]; !exists {
				logrus.Info(
					fmt.Sprintf(
						"%s does not exist in %s tserver gflags to edit, ignoring\n", key, cluster))
			} else {
				tserverGflags[key] = value
			}
		}
	}

	if len(strings.TrimSpace(removeGFlagsString)) != 0 {
		for _, key := range strings.Split(removeGFlagsString, ",") {
			if _, exists := tserverGflags[key]; !exists {
				logrus.Info(
					fmt.Sprintf(
						"%s does not exist in %s tsever gflags to remove, ignoring\n", key, cluster))
			} else {
				delete(tserverGflags, key)
			}
		}
	}

	return tserverGflags, nil
}
