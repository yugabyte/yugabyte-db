/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { YBAHost } from '../../../redesign/helpers/constants';
import { HostInfo, Universe } from '../../../redesign/helpers/dtos';
import {
  NTPSetupType,
  ProviderCode,
  CloudVendorProviders,
  KubernetesProvider,
  KUBERNETES_PROVIDERS_MAP,
  KubernetesProviderType
} from './constants';
import { K8sCertIssuerType } from './forms/configureRegion/constants';
import { SupportedAZField, SupportedRegionField } from './forms/configureRegion/types';
import { UniverseItem } from './providerView/providerDetails/UniverseTable';
import { AccessKey, K8sRegionCloudInfo, YBAvailabilityZone, YBProvider, YBRegion } from './types';

export const getCertIssuerType = (k8sRegionCloudInfo: K8sRegionCloudInfo) => {
  if (k8sRegionCloudInfo.certManagerClusterIssuer) {
    return K8sCertIssuerType.CLUSTER_ISSUER;
  }
  if (k8sRegionCloudInfo.certManagerIssuer) {
    return K8sCertIssuerType.ISSUER;
  }
  return K8sCertIssuerType.NONE;
};

export const getNtpSetupType = (providerConfig: YBProvider): NTPSetupType => {
  if (
    providerConfig.code === ProviderCode.KUBERNETES ||
    providerConfig.details.setUpChrony === false
  ) {
    return NTPSetupType.NO_NTP;
  }
  if (providerConfig.details.ntpServers.length) {
    return NTPSetupType.SPECIFIED;
  }
  return NTPSetupType.CLOUD_VENDOR;
};

// The public cloud providers (AWS, GCP, AZU) each provide their own NTP server which users may opt use
export const hasDefaultNTPServers = (providerCode: ProviderCode) =>
  (CloudVendorProviders as readonly ProviderCode[]).includes(providerCode);

// TODO: API should return the YBA host as part of the hostInfo response.
export const getYBAHost = (hostInfo: HostInfo) => {
  if (typeof hostInfo?.gcp !== 'string') {
    return YBAHost.GCP;
  }
  if (typeof hostInfo?.aws !== 'string') {
    return YBAHost.AWS;
  }
  return YBAHost.SELF_HOSTED;
};

export const getInfraProviderTab = (providerConfig: YBProvider) => {
  // Kubernetes providers are handled as a special case here because the UI
  // exposes 3 tabs for kubernetes providers.
  // - VMware Tanzu
  // - Red Hat OpenShift
  // - Managed Kubernetes
  // Moreover, the deprecated kubernetes provider types are not given their own
  // tab. These fall under `KubernetesProviderType.MANAGED_SERVICE` as read-only.
  // We do not enable the option to create new providers using deprecated kubernetes
  // provider types, nor do we allow changing an existing provider to use a deprecated
  // kubernetes provider type.

  if (providerConfig.code === ProviderCode.KUBERNETES) {
    const kubernetesProviderType = getKubernetesProviderType(
      providerConfig.details.cloudInfo.kubernetes.kubernetesProvider
    );
    return kubernetesProviderType === KubernetesProviderType.DEPRECATED
      ? KubernetesProviderType.MANAGED_SERVICE
      : kubernetesProviderType;
  }
  return providerConfig.code;
};

export const getKubernetesProviderType = (kubernetesProvider: KubernetesProvider) =>
  Object.keys(KUBERNETES_PROVIDERS_MAP).find((kubernetesProviderType) =>
    KUBERNETES_PROVIDERS_MAP[kubernetesProviderType].includes(kubernetesProvider)
  ) ?? kubernetesProvider;

export const getLinkedUniverses = (providerUUID: string, universes: Universe[]) =>
  universes.reduce((linkedUniverses: UniverseItem[], universe) => {
    const linkedClusters = universe.universeDetails.clusters.filter(
      (cluster) => cluster.userIntent.provider === providerUUID
    );
    if (linkedClusters.length) {
      linkedUniverses.push({
        ...universe,
        linkedClusters: linkedClusters
      });
    }
    return linkedUniverses;
  }, []);

/**
 * Returns a region code to availability zone mapping which captures all zones in which linked universes
 * have deployed instances.
 *
 * Assumptions:
 * - The universes are all created using the same provider.
 */
export const getRegionToInUseAz = (
  providerUuid: string,
  linkedUniverses: UniverseItem[]
): Map<string, Set<string>> => {
  const regionToInUseAz = new Map<string, Set<string>>();
  linkedUniverses.forEach((linkedUniverse) =>
    linkedUniverse.linkedClusters.forEach((linkedCluster) =>
      linkedCluster.placementInfo.cloudList
        .find((provider) => provider.uuid === providerUuid)
        ?.regionList.forEach((region) => {
          const azNames = regionToInUseAz.get(region.code);
          if (azNames === undefined) {
            regionToInUseAz.set(
              region.code,
              new Set<string>(region.azList.map((zone) => zone.name))
            );
          } else {
            region.azList.forEach((zone) => azNames.add(zone.name));
          }
        })
    )
  );
  return regionToInUseAz;
};

export const getInUseAzs = (
  providerUuid: string,
  linkedUniverses: UniverseItem[],
  regionCode: string | undefined
) => {
  const regionToInUseAz = getRegionToInUseAz(providerUuid, linkedUniverses);
  return (regionCode !== undefined && regionToInUseAz.get(regionCode)) || new Set<string>();
};

export const getInUseImageBundleUuids = (linkedUniverses: UniverseItem[]) => {
  const inUseImageBundleUuids = new Set<string>();
  linkedUniverses.forEach((linkedUniverse) =>
    linkedUniverse.linkedClusters.forEach((linkedCluster) => {
      inUseImageBundleUuids.add(linkedCluster.userIntent.imageBundleUUID);
    })
  );
  return inUseImageBundleUuids;
};

export const getLatestAccessKey = (accessKeys: AccessKey[]) =>
  accessKeys.reduce((latestAccessKey: null | AccessKey, currentAccessKey) => {
    if (!latestAccessKey) {
      return currentAccessKey;
    }
    const latestAccessKeyCreationTimestamp = Date.parse(latestAccessKey.creationDate);
    const currentAccessKeyCreationTimestamp = Date.parse(currentAccessKey.creationDate);

    return Number.isNaN(latestAccessKeyCreationTimestamp) ||
      currentAccessKeyCreationTimestamp > latestAccessKeyCreationTimestamp
      ? currentAccessKey
      : latestAccessKey;
  }, null);

export const findExistingRegion = <TYBProvider extends YBProvider, TYBRegion extends YBRegion>(
  providerConfig: TYBProvider,
  regionCode: string
): TYBRegion | undefined => {
  return (providerConfig.regions as TYBRegion[])?.find((region) => region.code === regionCode);
};

export const findExistingZone = <
  TYBRegion extends YBRegion,
  TYBAvailabilityZone extends YBAvailabilityZone
>(
  existingRegion: TYBRegion | undefined,
  zoneCode: string
): TYBAvailabilityZone | undefined => {
  return (existingRegion?.zones as TYBAvailabilityZone[])?.find((zone) => zone.code === zoneCode);
};

const deleteZone = <TYBAvailabilityZone extends YBAvailabilityZone>(zone: TYBAvailabilityZone) => ({
  ...zone,
  active: false
});

const deleteRegion = <TYBRegion extends YBRegion>(region: TYBRegion) => ({
  ...region,
  active: false,
  zones: region.zones.map((zone) => deleteZone(zone))
});

export const getDeletedRegions = <
  TYBRegion extends YBRegion,
  TSupportedRegionField extends SupportedRegionField
>(
  existingRegions: TYBRegion[],
  requestedRegions: TSupportedRegionField[]
) => {
  const persistedRegionCodes = requestedRegions.map((region) => region.code);

  return existingRegions
    .filter((region) => !persistedRegionCodes.includes(region.code))
    .map((region) => deleteRegion(region));
};

export const getDeletedZones = <
  TYBAvailabilityZone extends YBAvailabilityZone,
  TSupportedAZField extends SupportedAZField
>(
  existingZones: TYBAvailabilityZone[] | undefined,
  requestedZones: TSupportedAZField[]
) => {
  const persistedZoneCodes = requestedZones.map((zone) => zone.code);

  return existingZones
    ? existingZones
        .filter((zone) => !persistedZoneCodes.includes(zone.code))
        .map((zone) => deleteZone(zone))
    : [];
};
