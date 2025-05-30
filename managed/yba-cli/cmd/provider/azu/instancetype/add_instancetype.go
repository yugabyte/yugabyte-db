/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetype

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil/instancetypeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// addInstanceTypesCmd represents the provider command
var addInstanceTypesCmd = &cobra.Command{
	Use:     "add",
	Aliases: []string{"create"},
	Short:   "Add an instance type to YugabyteDB Anywhere Azure provider",
	Long:    "Add an instance type to YugabyteDB Anywhere Azure provider",
	Example: `yba provider azure instance-type add \
	--name <provider-name> --instance-type-name <instance-type>\
	--volume mount-points=<mount-point>::size=<size>::type=<volume-type>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		instancetypeutil.AddAndListInstanceTypeValidations(cmd, "add")
	},
	Run: func(cmd *cobra.Command, args []string) {
		instancetypeutil.AddInstanceTypeUtil(
			cmd,
			util.AzureProviderType,
			"Azure",
			"Azure",
		)
	},
}

func init() {
	addInstanceTypesCmd.Flags().SortFlags = false

	addInstanceTypesCmd.Flags().String("instance-type-name", "",
		"[Required] Instance type name.")
	addInstanceTypesCmd.Flags().StringArray("volume", []string{},
		"[Optional] Volumes associated per node of an instance type."+
			" Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"type=<volume-type>::"+
			"size=<volume-size>::mount-points=<comma-separated-mount-points>\". "+
			formatter.Colorize("Mount points is a required key-value.",
				formatter.GreenColor)+
			" Volume type (Defaults to SSD, Allowed values: EBS, SSD, HDD, NVME)"+
			" and Volume size (Defaults to 100) are optional. "+
			"Each volume needs to be added using a separate --volume flag.")
	addInstanceTypesCmd.Flags().Float64("mem-size", 8,
		"[Optional] Memory size of the node in GB.")
	addInstanceTypesCmd.Flags().Float64("num-cores", 4,
		"[Optional] Number of cores per node.")
	addInstanceTypesCmd.Flags().String("tenancy", "",
		"[Optional] Tenancy of the nodes of this type. Allowed values (case sensitive): "+
			"Shared, Dedicated, Host.")

	addInstanceTypesCmd.MarkFlagRequired("instance-type-name")

}
