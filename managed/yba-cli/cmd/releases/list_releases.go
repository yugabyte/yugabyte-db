/*
 * Copyright (c) YugaByte, Inc.
 */

package releases

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/releases"
	"golang.org/x/exp/slices"
)

var listReleasesCmd = &cobra.Command{
	Use:   "list",
	Short: "List YugabyteDB version releases",
	Long:  "List YugabyteDB version releases",
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		releasesListRequest := authAPI.GetListOfReleases(true)

		r, response, err := releasesListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Releases", "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		sortedReleases := SortReleasesWithMetadata(r)
		releasesCtx := formatter.Context{
			Output: os.Stdout,
			Format: releases.NewReleasesFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			fmt.Println("No releases found")
			return
		}

		releases.Write(releasesCtx, sortedReleases)

	},
}

func init() {
}

// SortReleasesWithMetadata compares and creates a list of sorted YugabyteDB releases
func SortReleasesWithMetadata(
	r map[string]map[string]interface{},
) []map[string]interface{} {
	sorted := make([]map[string]interface{}, 0)

	versions := make([]string, 0)
	for v := range r {
		versions = append(versions, v)
	}

	// the function as described in the documentation is the less function,
	// but for the purpose of getting the latest release, it's described as
	// a function returning the greater of the 2 versions
	slices.SortStableFunc(versions, func(x, y string) int {
		compare, err := util.CompareYbVersions(x, y)
		if err != nil {
			return 0
		}
		return compare * -1
	})

	for _, key := range versions {
		r[key]["version"] = key
		sorted = append(sorted, r[key])
	}

	return sorted
}
