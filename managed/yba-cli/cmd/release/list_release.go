/*
 * Copyright (c) YugabyteDB, Inc.
 */

package release

import (
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/release/releaseutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/release"

	"golang.org/x/exp/slices"
)

var listReleaseCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB versions",
	Long:    "List YugabyteDB versions",
	Example: `yba yb-db-version list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		releaseutil.IsCommandAllowed(authAPI)

		releasesListRequest := authAPI.ListNewReleases()

		deploymentType, err := cmd.Flags().GetString("deployment-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(deploymentType) {
			releasesListRequest = releasesListRequest.DeploymentType(
				strings.ToLower(deploymentType))
		}

		rList, response, err := releasesListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "List")
		}

		r := make([]ybaclient.ResponseRelease, 0)

		releaseType, err := cmd.Flags().GetString("type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(releaseType) {
			for _, v := range rList {
				if strings.Compare(v.GetReleaseType(), strings.ToUpper(releaseType)) == 0 {
					r = append(r, v)
				}
			}
		} else {
			r = rList
		}

		sortedReleases := SortReleasesWithMetadata(r)
		releaseCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  release.NewReleaseFormat(viper.GetString("output")),
		}
		if len(sortedReleases) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Infoln("No YugabyteDB versions found\n")
			} else {
				logrus.Infoln("[]\n")
			}
			return
		}

		release.Write(releaseCtx, sortedReleases)

	},
}

func init() {
	listReleaseCmd.Flags().SortFlags = false

	listReleaseCmd.Flags().String("deployment-type", "",
		"[Optional] Deployment type of the YugabyteDB version. "+
			"Allowed values: x86_64, aarch64, kubernetes")
	listReleaseCmd.Flags().String("type", "",
		"[Optional] Release type. Allowed values: lts, sts, preview")
}

// SortReleasesWithMetadata compares and creates a list of sorted YugabyteDB releases
func SortReleasesWithMetadata(
	r []ybaclient.ResponseRelease,
) []ybaclient.ResponseRelease {
	sorted := make([]ybaclient.ResponseRelease, 0)

	versionsStable := make([]ybaclient.ResponseRelease, 0)
	versionsPreview := make([]ybaclient.ResponseRelease, 0)
	for _, v := range r {
		if util.IsVersionStable(v.GetVersion()) {
			versionsStable = append(versionsStable, v)
		} else {
			versionsPreview = append(versionsPreview, v)
		}
	}

	// the function as described in the documentation is the less function,
	// but for the purpose of getting the latest release, it's described as
	// a function returning the greater of the 2 versions
	slices.SortStableFunc(versionsStable, func(x, y ybaclient.ResponseRelease) int {
		compare, err := util.CompareYbVersions(x.GetVersion(), y.GetVersion())
		if err != nil {
			return 0
		}
		return compare * -1
	})
	slices.SortStableFunc(versionsPreview, func(x, y ybaclient.ResponseRelease) int {
		compare, err := util.CompareYbVersions(x.GetVersion(), y.GetVersion())
		if err != nil {
			return 0
		}
		return compare * -1
	})

	sorted = append(sorted, versionsStable...)

	sorted = append(sorted, versionsPreview...)

	return sorted
}
