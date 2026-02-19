/*
 * Copyright (c) YugabyteDB, Inc.
 */

package customer

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/customer"
)

var describeCustomerCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get", "details"},
	Short:   "Describe details of the current YugabyteDB Anywhere customer",
	Long:    "Describe details of the current YugabyteDB Anywhere customer",
	Example: `yba customer describe`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		customerDetails, response, err := authAPI.CustomerDetail().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Customer", "Describe")
		}

		customerDetail := util.CheckAndDereference(
			customerDetails,
			"Current customer details not found",
		)

		if customerDetail.GetUuid() == "" {
			logrus.Fatalf(
				formatter.Colorize("Current customer details not found\n", formatter.RedColor),
			)
		}

		r := make([]ybaclient.CustomerDetailsData, 0)
		r = append(r, customerDetail)

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullCustomerContext := *customer.NewFullCustomerContext()
			fullCustomerContext.Output = os.Stdout
			fullCustomerContext.Format = customer.NewFullCustomerFormat(
				viper.GetString("output"),
			)
			fullCustomerContext.SetFullCustomer(r[0])
			fullCustomerContext.Write()
			return
		}

		customerCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  customer.NewCustomerFormat(viper.GetString("output")),
		}
		customer.Write(customerCtx, r)
	},
}

func init() {
	describeCustomerCmd.Flags().SortFlags = false
}
