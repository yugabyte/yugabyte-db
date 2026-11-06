/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/supportbundle"
)

var listSupportBundleV2Cmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List support bundles created via the v2 API",
	Long:    "List support bundles for a universe or YBA-only bundles on the platform host.",
	Example: `yba universe support-bundlev2 list --name <universe-name>

yba universe support-bundlev2 list`,
	PreRun: func(cmd *cobra.Command, args []string) {
		preflightValidateUniverse(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		target, err := resolveSupportBundleV2Target(cmd)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		bundles, err := listSupportBundlesV2(target)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !target.ybaOnly {
			supportbundle.Universe = target.universe
		}

		supportBundleCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  supportbundle.NewSupportBundleFormat(viper.GetString("output")),
		}
		if len(bundles) < 1 {
			logNoSupportBundlesFound(target)
			return
		}
		if err := supportbundle.Write(supportBundleCtx, bundles); err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
}

// supportBundleV2PageSize is the maximum page size the paged list API accepts.
const supportBundleV2PageSize = 500

func listSupportBundlesV2(target *supportBundleV2Target) ([]ybaclient.SupportBundle, error) {
	bundles := make([]ybaclient.SupportBundle, 0)
	offset := int32(0)

	for {
		query := ybav2client.PaginationSpec{
			Offset:    ybav2client.PtrInt32(offset),
			Limit:     ybav2client.PtrInt32(supportBundleV2PageSize),
			Direction: ybav2client.PtrString("DESC"),
		}
		reqBytes, err := json.Marshal(query)
		if err != nil {
			return nil, err
		}

		var respBytes []byte
		if target.ybaOnly {
			respBytes, err = target.authAPI.ListSupportBundleYbaV2Raw(reqBytes)
		} else {
			respBytes, err = target.authAPI.ListSupportBundleV2Raw(target.targetUUID, reqBytes)
		}
		if err != nil {
			return nil, err
		}

		page, err := parseSupportBundlePage(respBytes)
		if err != nil {
			return nil, err
		}
		for _, entity := range page.Entities {
			bundles = append(bundles, supportBundleV2ToClient(entity))
		}

		if !page.GetHasNext() || len(page.Entities) == 0 {
			return bundles, nil
		}
		offset += int32(len(page.Entities))
	}
}

// parseSupportBundlePage decodes one page of the paged-list response. restAPICall turns a non-2xx
// into an error before this is reached, so a failure never arrives here looking like an empty page.
func parseSupportBundlePage(
	respBytes []byte,
) (ybav2client.SupportBundlePagedResp, error) {
	var page ybav2client.SupportBundlePagedResp
	if err := json.Unmarshal(respBytes, &page); err != nil {
		return page, fmt.Errorf("unexpected list support bundle response: %w", err)
	}
	return page, nil
}

// supportBundleV2ToClient flattens a v2 { spec, info } response into the v1 shape that the shared
// internal/formatter/universe/supportbundle renderer takes, so the v1 and v2 support bundle
// commands print identically. It goes away when that formatter stops being shared with the v1
// command and can render the v2 model directly.
func supportBundleV2ToClient(b ybav2client.SupportBundle) ybaclient.SupportBundle {
	bundle := ybaclient.SupportBundle{}
	if info := b.Info; info != nil {
		bundle.BundleUUID = info.GetUuid()
		bundle.Status = string(info.GetStatus())
		bundle.CreationDate = info.CreationDate
		bundle.ExpirationDate = info.ExpirationDate
		bundle.SizeInBytes = info.SizeInBytes
		// scope_uuid is a nullable field the server sends as an explicit null for YBA-only
		// bundles, which leaves it "set" with an empty value. Falling through on the empty
		// string rather than on HasScopeUuid keeps absent, null and empty equivalent.
		if scope := info.GetScopeUuid(); !util.IsEmptyString(scope) {
			bundle.ScopeUUID = scope
		} else {
			bundle.ScopeUUID = info.GetCustomerUuid()
		}
	}
	if spec := b.Spec; spec != nil {
		bundle.StartDate = spec.StartDate
		bundle.EndDate = spec.EndDate
		bundle.BundleDetails = ybaclient.BundleDetails{
			Components:             componentTypesToStrings(spec.Components),
			MaxCoreFileSize:        spec.MaxCoreFileSize,
			MaxNumRecentCores:      spec.MaxNumRecentCores,
			PromDumpStartDate:      spec.PromDumpStartDate,
			PromDumpEndDate:        spec.PromDumpEndDate,
			PrometheusMetricsTypes: metricsTypesToStrings(spec.PrometheusMetricsTypes),
		}
	}
	return bundle
}
func componentTypesToStrings(types []ybav2client.SupportBundleComponentType) []string {
	if len(types) == 0 {
		return nil
	}
	values := make([]string, 0, len(types))
	for _, componentType := range types {
		values = append(values, string(componentType))
	}
	return values
}
func metricsTypesToStrings(types []ybav2client.PrometheusMetricsType) []string {
	if len(types) == 0 {
		return nil
	}
	values := make([]string, 0, len(types))
	for _, metricsType := range types {
		values = append(values, string(metricsType))
	}
	return values
}
func logNoSupportBundlesFound(target *supportBundleV2Target) {
	if target.ybaOnly {
		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Infof("No YBA-only support bundles found\n")
		} else {
			logrus.Info("[]\n")
		}
		return
	}
	if util.IsOutputType(formatter.TableFormatKey) {
		logrus.Infof(
			"No support bundles found for universe %s (%s)\n",
			target.targetName,
			target.targetUUID,
		)
	} else {
		logrus.Info("[]\n")
	}
}
