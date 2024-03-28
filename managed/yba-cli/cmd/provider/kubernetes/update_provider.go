/*
 * Copyright (c) YugaByte, Inc.
 */

package kubernetes

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateK8sProviderCmd represents the provider command
var updateK8sProviderCmd = &cobra.Command{
	Use:   "update",
	Short: "Update a Kubernetes YugabyteDB Anywhere provider",
	Long:  "Update a Kubernetes provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to update\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerListRequest := authAPI.GetListOfProviders()
		providerListRequest = providerListRequest.Name(providerName)

		r, response, err := providerListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Provider: Kubernetes", "Update - Fetch Providers")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		var provider ybaclient.Provider
		providerCode := util.K8sProviderType
		for _, p := range r {
			if p.GetCode() == providerCode {
				provider = p
			}
		}

		if len(strings.TrimSpace(provider.GetName())) == 0 {
			errMessage := fmt.Sprintf("No provider %s in cloud type %s.", providerName, providerCode)
			logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
		}

		providerRegions := provider.GetRegions()
		details := provider.GetDetails()
		cloudInfo := details.GetCloudInfo()
		k8sCloudInfo := cloudInfo.GetKubernetes()

		newProviderName, err := cmd.Flags().GetString("new-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(newProviderName) > 0 {
			logrus.Debug("Updating provider name\n")
			provider.SetName(newProviderName)
			providerName = newProviderName
		}

		// Updating CloudInfo

		providerType, err := cmd.Flags().GetString("type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(providerType)) > 0 {
			logrus.Debug("Updating kubernetes provider type\n")
			k8sCloudInfo.SetKubernetesProvider(providerType)
		}

		imageRegistry, err := cmd.Flags().GetString("image-registry")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(imageRegistry)) > 0 {
			logrus.Debug("Updating Image Registry\n")
			k8sCloudInfo.SetKubernetesImageRegistry(imageRegistry)
		}

		pullSecretFilePath, err := cmd.Flags().GetString("pull-secret-file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(pullSecretFilePath)) > 0 {
			logrus.Debug("Reading Kubernetes Pull Secret\n")
			pullSecretContent := util.YAMLtoString(pullSecretFilePath)
			if len(strings.TrimSpace(pullSecretContent)) > 0 {
				logrus.Debug("Updating Kubernetes Pull Secret\n")
				k8sCloudInfo.SetKubernetesPullSecret(pullSecretContent)
			}
		}

		configFilePath, err := cmd.Flags().GetString("kubeconfig-file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(configFilePath)) > 0 {
			logrus.Debug("Reading Kube Config\n")
			configContent := util.YAMLtoString(configFilePath)
			if len(strings.TrimSpace(configContent)) > 0 {
				logrus.Debug("Updating Kube Config\n")
				k8sCloudInfo.SetKubeConfigContent(configContent)
			}
		}

		storageClass, err := cmd.Flags().GetString("storage-class")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(storageClass)) > 0 {
			logrus.Debug("Updating Kubernetes storage class\n")
			k8sCloudInfo.SetKubernetesStorageClass(storageClass)
		}

		cloudInfo.SetKubernetes(k8sCloudInfo)
		details.SetCloudInfo(cloudInfo)

		// End of Updating CloudInfo

		// Update ProviderDetails

		if cmd.Flags().Changed("airgap-install") {
			logrus.Debug("Updating airgap install\n")
			airgapInstall, err := cmd.Flags().GetBool("airgap-install")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			details.SetAirGapInstall(airgapInstall)
		}

		provider.SetDetails(details)

		// End of Updating ProviderDetails

		// Update Regions

		addRegions, err := cmd.Flags().GetStringArray("add-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		addZones, err := cmd.Flags().GetStringArray("add-zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		editRegions, err := cmd.Flags().GetStringArray("edit-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		editZones, err := cmd.Flags().GetStringArray("edit-zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		removeRegions, err := cmd.Flags().GetStringArray("remove-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		removeZones, err := cmd.Flags().GetStringArray("remove-zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerRegions = removeK8sRegions(removeRegions, providerRegions)

		providerRegions = editK8sRegions(editRegions, addZones, editZones, removeZones, providerRegions)

		providerRegions = addK8sRegions(addRegions, addZones, providerRegions)

		provider.SetRegions(providerRegions)
		// End of Updating Regions

		rUpdate, response, err := authAPI.EditProvider(provider.GetUuid()).
			EditProviderRequest(provider).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response,
				err,
				"Provider: Kubernetes",
				"Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rUpdate.GetResourceUUID()
		taskUUID := rUpdate.GetTaskUUID()

		providerutil.WaitForUpdateProviderTask(authAPI,
			providerName, providerUUID, providerCode, taskUUID)
	},
}

func init() {
	updateK8sProviderCmd.Flags().SortFlags = false

	updateK8sProviderCmd.Flags().String("new-name", "",
		"[Optional] Updating provider name.")

	updateK8sProviderCmd.Flags().String("type", "",
		"[Optional] Updating kubernetes cloud type. Allowed values: aks, eks, gke, custom.")

	updateK8sProviderCmd.Flags().String("image-registry", "",
		"[Optional] Updating kubernetes Image Registry.")

	updateK8sProviderCmd.Flags().String("pull-secret-file", "",
		"[Optional] Updating kuberenetes Pull Secret File Path.")

	updateK8sProviderCmd.Flags().String("kubeconfig-file", "",
		"[Optional] Updating kuberenetes Config File Path.")

	updateK8sProviderCmd.Flags().String("storage-class", "",
		"[Optional] Updating kubernetes Storage Class.")

	updateK8sProviderCmd.Flags().StringArray("add-region", []string{},
		"[Optional] Add region associated with the Kubernetes provider."+
			" Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,"+
			"config-file-path=<path-for-the-kubernetes-region-config-file>,"+
			"storage-class=<storage-class>,"+
			"cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,"+
			"cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,"+
			"pod-address-template=<pod-address-template>,"+
			"overrirdes-file-path=<path-for-file-contanining-overries>\". "+
			formatter.Colorize("Region name is a required key-value.",
				formatter.GreenColor)+
			" Config File Path, Storage Class, Cert Manager"+
			" Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and"+
			" Overrides File Path are optional. "+
			"Each region needs to be added using a separate --add-region flag.")

	updateK8sProviderCmd.Flags().StringArray("add-zone", []string{},
		"[Optional] Add zone associated to the Kubernetes Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,"+
			"config-file-path=<path-for-the-kubernetes-region-config-file>,"+
			"storage-class=<storage-class>,"+
			"cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,"+
			"cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,"+
			"pod-address-template=<pod-address-template>,"+
			"overrirdes-file-path=<path-for-file-contanining-overries>\". "+
			formatter.Colorize("Zone name and Region name are required values. ",
				formatter.GreenColor)+
			" Config File Path, Storage Class, Cert Manager"+
			" Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and"+
			" Overrides File Path are optional. "+
			"Each --add-region definition "+
			"must have atleast one corresponding --add-zone definition. Multiple --add-zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --add-zone flag.")

	updateK8sProviderCmd.Flags().StringArray("edit-region", []string{},
		"[Optional] Edit region associated with the Kubernetes provider. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,"+
			"config-file-path=<path-for-the-kubernetes-region-config-file>,"+
			"storage-class=<storage-class>,"+
			"cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,"+
			"cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,"+
			"pod-address-template=<pod-address-template>,"+
			"overrirdes-file-path=<path-for-file-contanining-overries>\". "+
			formatter.Colorize("Region name is a required key-value.",
				formatter.GreenColor)+
			" Config File Path, Storage Class, Cert Manager"+
			" Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and"+
			" Overrides File Path are optional. "+
			"Each region needs to be edited using a separate --edit-region flag.")

	updateK8sProviderCmd.Flags().StringArray("edit-zone", []string{},
		"[Optional] Edit zone associated to the Kubernetes Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,"+
			"config-file-path=<path-for-the-kubernetes-region-config-file>,"+
			"storage-class=<storage-class>,"+
			"cert-manager-cluster-issuer=<cert-manager-cluster-issuer>,"+
			"cert-manager-issuer=<cert-manager-issuer>,domain=<domain>,namespace=<namespace>,"+
			"pod-address-template=<pod-address-template>,"+
			"overrirdes-file-path=<path-for-file-contanining-overries>\". "+
			formatter.Colorize("Zone name and Region name are required values. ",
				formatter.GreenColor)+
			"Config File Path, Storage Class, Cert Manager"+
			" Cluster Issuer, Cert Manager Issuer, Domain, Namespace, Pod Address Template and"+
			" Overrides File Path are optional. "+
			"Each zone needs to be modified using a separate --edit-zone flag.")

	updateK8sProviderCmd.Flags().StringArray("remove-region", []string{},
		"[Optional] Region name to be removed from the provider. "+
			"Each region to be removed needs to be provided using a separate "+
			"--remove-region definition. Removing a region removes the corresponding zones.")
	updateK8sProviderCmd.Flags().StringArray("remove-zone", []string{},
		"[Optional] Remove zone associated to the Kubernetes Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Each zone needs to be removed using a separate --remove-zone flag.")

	updateK8sProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Do YugabyteDB nodes have access to public internet to download packages.")
}

func removeK8sRegions(
	removeRegions []string,
	providerRegions []ybaclient.Region) []ybaclient.Region {
	if len(removeRegions) == 0 {
		return providerRegions
	}

	for _, r := range removeRegions {
		for i, pR := range providerRegions {
			if strings.Compare(pR.GetCode(), r) == 0 {
				pR.SetActive(false)
				providerRegions[i] = pR
			}
		}
	}

	return providerRegions
}

func editK8sRegions(
	editRegions, addZones, editZones, removeZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {

	for i, r := range providerRegions {
		regionName := r.GetCode()
		zones := r.GetZones()
		zones = removeK8sZones(regionName, removeZones, zones)
		zones = editK8sZones(regionName, editZones, zones)
		zones = addK8sZones(regionName, addZones, zones)
		r.SetZones(zones)
		if len(editRegions) != 0 {
			for _, regionString := range editRegions {
				region := providerutil.BuildRegionMapFromString(regionString, "edit")

				if strings.Compare(region["name"], regionName) == 0 {
					details := r.GetDetails()
					cloudInfo := details.GetCloudInfo()
					k8s := cloudInfo.GetKubernetes()
					if len(region["domain"]) != 0 {
						k8s.SetKubeDomain(region["domain"])
					}
					if len(region["cert-manager-cluster-issuer"]) != 0 {
						k8s.SetKubeDomain(region["cert-manager-cluster-issuer"])
					}
					if len(region["cert-manager-issuer"]) != 0 {
						k8s.SetKubeDomain(region["cert-manager-issuer"])
					}
					if len(region["namespace"]) != 0 {
						k8s.SetKubeDomain(region["namespace"])
					}
					if len(region["pod-address-template"]) != 0 {
						k8s.SetKubeDomain(region["pod-address-template"])
					}
					if len(region["storage-class"]) != 0 {
						k8s.SetKubeDomain(region["storage-class"])
					}
					if filePath, ok := region["config-file-path"]; ok {
						var configContent string
						if strings.TrimSpace(filePath) != "" {
							logrus.Debug("Reading Region Kube Config\n")
							configContent = util.YAMLtoString(filePath)
						}
						if len(strings.TrimSpace(configContent)) != 0 {
							k8s.SetKubeConfigContent(configContent)
						}
					}
					if filePath, ok := region["overrirdes-file-path"]; ok {
						var overrides string
						if strings.TrimSpace(filePath) != "" {
							logrus.Debug("Reading Region Kubernetes Overrides\n")
							overrides = util.YAMLtoString(filePath)
						}
						if len(strings.TrimSpace(overrides)) != 0 {
							k8s.SetOverrides(overrides)
						}
					}

					cloudInfo.SetKubernetes(k8s)
					details.SetCloudInfo(cloudInfo)
					r.SetDetails(details)

				}

			}
		}
		providerRegions[i] = r
	}
	return providerRegions
}

func addK8sRegions(
	addRegions, addZones []string,
	providerRegions []ybaclient.Region) []ybaclient.Region {
	for _, regionString := range addRegions {
		region := providerutil.BuildRegionMapFromString(regionString, "")
		if _, ok := region["name"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Name not specified in region.\n",
					formatter.RedColor))
		}
		var configContent string
		if filePath, ok := region["config-file-path"]; ok {
			if strings.TrimSpace(filePath) != "" {
				logrus.Debug("Reading Region Kube Config\n")
				configContent = util.YAMLtoString(filePath)
			}
		}

		var overrides string
		if filePath, ok := region["overrirdes-file-path"]; ok {
			if strings.TrimSpace(filePath) != "" {
				logrus.Debug("Reading Region Kubernetes Overrides\n")
				overrides = util.YAMLtoString(filePath)
			}
		}

		zones := addK8sZones(region["name"], addZones, make([]ybaclient.AvailabilityZone, 0))
		r := ybaclient.Region{
			Code: util.GetStringPointer(region["name"]),
			Name: util.GetStringPointer(region["name"]),
			Details: &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Kubernetes: &ybaclient.KubernetesRegionInfo{
						CertManagerClusterIssuer: util.GetStringPointer(region["cert-manager-cluster-issuer"]),
						CertManagerIssuer:        util.GetStringPointer(region["cert-manager-issuer"]),
						KubeConfigContent:        util.GetStringPointer(configContent),
						KubeDomain:               util.GetStringPointer(region["domain"]),
						KubeNamespace:            util.GetStringPointer(region["namespace"]),
						KubePodAddressTemplate:   util.GetStringPointer(region["pod-address-template"]),
						KubernetesStorageClass:   util.GetStringPointer(region["storage-class"]),
						Overrides:                util.GetStringPointer(overrides),
					},
				},
			},
			Zones: zones,
		}
		providerRegions = append(providerRegions, r)
	}
	return providerRegions
}

func removeK8sZones(
	regionName string,
	removeZones []string,
	zones []ybaclient.AvailabilityZone,
) []ybaclient.AvailabilityZone {
	if len(removeZones) == 0 {
		if len(zones) == 0 {
			logrus.Fatalln(
				formatter.Colorize("Atleast one zone is required per region.\n",
					formatter.RedColor))
		}
		return zones
	}
	for _, zoneString := range removeZones {
		zone := providerutil.BuildZoneMapFromString(zoneString, "remove")

		if strings.Compare(zone["region-name"], regionName) == 0 {
			for i, az := range zones {
				if strings.Compare(az.GetCode(), zone["name"]) == 0 {
					az.SetActive(false)
					zones[i] = az
				}
			}
		}
	}
	if len(zones) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}
	return zones
}

func editK8sZones(
	regionName string,
	editZones []string,
	zones []ybaclient.AvailabilityZone,
) []ybaclient.AvailabilityZone {
	if len(editZones) == 0 {
		if len(zones) == 0 {
			logrus.Fatalln(
				formatter.Colorize("Atleast one zone is required per region.\n",
					formatter.RedColor))
		}
		return zones
	}
	for _, zoneString := range editZones {
		zone := providerutil.BuildZoneMapFromString(zoneString, "edit")

		if strings.Compare(zone["region-name"], regionName) == 0 {
			for i, az := range zones {
				if strings.Compare(az.GetCode(), zone["name"]) == 0 {
					details := az.GetDetails()
					cloudInfo := details.GetCloudInfo()
					k8s := cloudInfo.GetKubernetes()
					if len(zone["domain"]) != 0 {
						k8s.SetKubeDomain(zone["domain"])
					}
					if len(zone["cert-manager-cluster-issuer"]) != 0 {
						k8s.SetKubeDomain(zone["cert-manager-cluster-issuer"])
					}
					if len(zone["cert-manager-issuer"]) != 0 {
						k8s.SetKubeDomain(zone["cert-manager-issuer"])
					}
					if len(zone["namespace"]) != 0 {
						k8s.SetKubeDomain(zone["namespace"])
					}
					if len(zone["pod-address-template"]) != 0 {
						k8s.SetKubeDomain(zone["pod-address-template"])
					}
					if len(zone["storage-class"]) != 0 {
						k8s.SetKubeDomain(zone["storage-class"])
					}
					if filePath, ok := zone["config-file-path"]; ok {
						var configContent string
						if strings.TrimSpace(filePath) != "" {
							logrus.Debug("Reading Region Kube Config\n")
							configContent = util.YAMLtoString(filePath)
						}
						if len(strings.TrimSpace(configContent)) != 0 {
							k8s.SetKubeConfigContent(configContent)
						}
					}
					if filePath, ok := zone["overrirdes-file-path"]; ok {
						var overrides string
						if strings.TrimSpace(filePath) != "" {
							logrus.Debug("Reading Region Kubernetes Overrides\n")
							overrides = util.YAMLtoString(filePath)
						}
						if len(strings.TrimSpace(overrides)) != 0 {
							k8s.SetOverrides(overrides)
						}
					}

					cloudInfo.SetKubernetes(k8s)
					details.SetCloudInfo(cloudInfo)
					az.SetDetails(details)
					zones[i] = az
				}
			}
		}
	}
	if len(zones) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}

	return zones
}

func addK8sZones(
	regionName string,
	addZones []string,
	zones []ybaclient.AvailabilityZone) []ybaclient.AvailabilityZone {
	if len(addZones) == 0 {
		if len(zones) == 0 {
			logrus.Fatalln(
				formatter.Colorize("Atleast one zone is required per region.\n",
					formatter.RedColor))
		}
		return zones
	}
	for _, zoneString := range addZones {
		zone := providerutil.BuildZoneMapFromString(zoneString, "")
		var configContent string
		if filePath, ok := zone["config-file-path"]; ok {
			if strings.TrimSpace(filePath) != "" {
				logrus.Debug("Reading Region Kube Config\n")
				configContent = util.YAMLtoString(filePath)
			}
		}
		var overrides string
		if filePath, ok := zone["overrirdes-file-path"]; ok {
			if strings.TrimSpace(filePath) != "" {
				logrus.Debug("Reading Region Kubernetes Overrides\n")
				overrides = util.YAMLtoString(filePath)
			}
		}
		if strings.Compare(zone["region-name"], regionName) == 0 {
			z := ybaclient.AvailabilityZone{
				Code: util.GetStringPointer(zone["name"]),
				Name: zone["name"],
				Details: &ybaclient.AvailabilityZoneDetails{
					CloudInfo: &ybaclient.AZCloudInfo{
						Kubernetes: &ybaclient.KubernetesRegionInfo{
							CertManagerClusterIssuer: util.GetStringPointer(zone["cert-manager-cluster-issuer"]),
							CertManagerIssuer:        util.GetStringPointer(zone["cert-manager-issuer"]),
							KubeConfigContent:        util.GetStringPointer(configContent),
							KubeDomain:               util.GetStringPointer(zone["domain"]),
							KubeNamespace:            util.GetStringPointer(zone["namespace"]),
							KubePodAddressTemplate:   util.GetStringPointer(zone["pod-address-template"]),
							KubernetesStorageClass:   util.GetStringPointer(zone["storage-class"]),
							Overrides:                util.GetStringPointer(overrides),
						},
					},
				},
			}
			zones = append(zones, z)
		}
	}
	if len(zones) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}
	return zones
}
