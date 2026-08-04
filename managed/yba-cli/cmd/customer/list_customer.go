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

var listCustomerCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List all YugabyteDB Anywhere customers",
	Long:    "List all YugabyteDB Anywhere customers",
	Example: `yba customer list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		customerListRequest := authAPI.ListOfCustomers()

		rCustomers, response, err := customerListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Customer", "List")
		}

		r := make([]ybaclient.CustomerDetailsData, 0)
		for _, customer := range rCustomers {
			customerDetails := ybaclient.CustomerDetailsData{
				Uuid:         customer.Uuid,
				Name:         customer.Name,
				CreationDate: customer.CreationDate,
				CustomerId:   util.GetInt32Pointer(int32(customer.GetCustomerId())),
				Code:         customer.Code,
			}
			r = append(r, customerDetails)
		}

		customerCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  customer.NewCustomerFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No customers found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		customer.Write(customerCtx, r)

	},
}

func init() {
	listCustomerCmd.Flags().SortFlags = false
}
