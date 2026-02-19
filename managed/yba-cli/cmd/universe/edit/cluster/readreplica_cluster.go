/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cluster

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// editReadReplicaClusterCmd represents the universe command
var editReadReplicaClusterCmd = &cobra.Command{
	Use:     "read-replica",
	Aliases: []string{"async"},
	Short:   "Edit the Read replica Cluster in a YugabyteDB Anywhere universe",
	Long:    "Edit the Read replica Cluster in a YugabyteDB Anywhere universe.",
	Example: `yba universe edit cluster --name <universe-name> read-replica --num-nodes 1 --replication-factor 1`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to edit\n", formatter.RedColor))
		}

		// Validations before upgrade operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.EditOperation)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to edit the Read replica Cluster %s: %s",
				"universe", universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, req, err := editClusterUtil(cmd, util.ReadReplicaCluster)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		rEdit, response, err := authAPI.UpdateReadOnlyCluster(universe.GetUniverseUUID()).
			UniverseConfigureTaskParams(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Edit Read Replica Cluster")
		}

		waitForEditClusterTask(
			authAPI,
			universe.GetName(),
			universe.GetUniverseUUID(),
			rEdit)
	},
}

func init() {
	editReadReplicaClusterCmd.Flags().SortFlags = false

	editReadReplicaClusterCmd.Flags().Int("num-nodes", 0,
		"[Optional] Number of nodes in the cluster.")
	editReadReplicaClusterCmd.Flags().Int("replication-factor", 0,
		"[Optional] Replication factor of the cluster.")
	editReadReplicaClusterCmd.Flags().String("add-regions", "",
		"[Optional] Add regions for the nodes of the cluster to be placed in. "+
			"Provide comma-separated strings in the following format: "+
			"\"--regions 'region-1-for-rr-cluster,region-2-for-rr-cluster'\"")
	editReadReplicaClusterCmd.Flags().String("remove-regions", "",
		"[Optional] Remove regions from the cluster. "+
			"Provide comma-separated strings in the following format: "+
			"\"--regions 'region-1-for-rr-cluster,region-2-for-rr-cluster'\"")

	editReadReplicaClusterCmd.Flags().StringArray("add-zones", []string{},
		"[Optional] Add zones for the nodes of the cluster to be placed in. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"--add-zones 'zone-name=<zone1>::region-name=<region1>::num-nodes=<number-of-nodes-to-be-placed-in-zone>\" "+
			"Each zone must have the region and number of nodes to be placed in that zone. "+
			"Add the region via --add-regions flag if not present in the universe. "+
			"Each zone needs to be added using a separate --add-zones flag.")
	editReadReplicaClusterCmd.Flags().StringArray("edit-zones", []string{},
		"[Optional] Edit number of nodes in the zone for the cluster. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"--edit-zones 'zone-name=<zone1>::region-name=<region1>::num-nodes=<number-of-nodes-to-be-placed-in-zone>\" "+
			"Each zone must have the region and number of nodes to be placed in that zone. "+
			"Each zone needs to be edited using a separate --edit-zones flag.")
	editReadReplicaClusterCmd.Flags().StringArray("remove-zones", []string{},
		"[Optional] Remove zones from the cluster. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"--remove-zones 'zone-name=<zone1>::region-name=<region1>\" "+
			"Each zone must have the region mentioned. "+
			"Each zone needs to be removed using a separate --remove-zones flag.")

	editReadReplicaClusterCmd.Flags().String("instance-type", "",
		"[Optional] Instance Type for the universe nodes.")
	editReadReplicaClusterCmd.Flags().Int("num-volumes", 0,
		"[Optional] Number of volumes to be mounted on this instance at the default path."+
			" Editing number of volumes is allowed only while changing instance-types.")
	editReadReplicaClusterCmd.Flags().Int("volume-size", 0,
		"[Optional] The size of each volume in each instance. "+
			"Editing volume size is allowed only while changing instance-types.")
	editReadReplicaClusterCmd.Flags().String("storage-type", "",
		"[Optional] Storage type used for this instance.")
	editReadReplicaClusterCmd.Flags().String("storage-class", "",
		"[Optional] Storage classs used for this instance, supported for Kubernetes.")

	editReadReplicaClusterCmd.Flags().StringToString("add-user-tags",
		map[string]string{}, "[Optional] Add User Tags to the DB instances. Provide "+
			"as key=value pairs per flag. Example \"--user-tags "+
			"name=test --user-tags owner=development\" OR "+
			"\"--user-tags name=test,owner=development\".")

	editReadReplicaClusterCmd.Flags().StringToString("edit-user-tags",
		map[string]string{}, "[Optional] Edit existing User Tags in the DB instances. Provide "+
			"as key=value pairs per flag. Example \"--user-tags "+
			"name=test --user-tags owner=development\" OR "+
			"\"--user-tags name=test,owner=development\".")

	editReadReplicaClusterCmd.Flags().String("remove-user-tags", "",
		"[Optional] Remove User tags from existing list in the DB instances. "+
			"Provide comma-separated values in the following format: "+
			"\"--remove-user-tags user-tag-key-1,user-tag-key-2\".")
}
