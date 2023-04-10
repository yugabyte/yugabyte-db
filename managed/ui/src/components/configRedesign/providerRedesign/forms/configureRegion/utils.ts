/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { ProviderCode } from '../../constants';
import {
  AWS_REGIONS,
  AZURE_REGIONS,
  GCP_REGIONS,
  KUBERNETES_REGIONS,
  ON_PREM_LOCATIONS,
  ON_PREM_UNLISTED_LOCATION
} from '../../providerRegionsData';

export const getRegionOptions = (providerCode: ProviderCode) => {
  switch (providerCode) {
    case ProviderCode.AWS:
      return Object.entries(AWS_REGIONS).map(([regionCode, regionDetails]) => ({
        value: { code: regionCode, zoneOptions: regionDetails.zones },
        label: getRegionlabel(providerCode, regionCode)
      }));
    case ProviderCode.AZU:
      return Object.entries(AZURE_REGIONS).map(([regionCode, regionDetails]) => ({
        value: { code: regionCode, zoneOptions: regionDetails.zones },
        label: getRegionlabel(providerCode, regionCode)
      }));
    case ProviderCode.GCP:
      return Object.entries(GCP_REGIONS).map(([regionCode, regionDetails]) => ({
        value: { code: regionCode, zoneOptions: regionDetails.zones },
        label: getRegionlabel(providerCode, regionCode)
      }));
    case ProviderCode.KUBERNETES:
      return Object.entries(KUBERNETES_REGIONS).map(([regionCode]) => ({
        value: { code: regionCode, zoneOptions: [] },
        label: getRegionlabel(providerCode, regionCode)
      }));
    default:
      return [];
  }
};

export const getRegionlabel = (providerCode: ProviderCode, regionCode: string) => {
  switch (providerCode) {
    case ProviderCode.AWS:
      return regionCode;
    case ProviderCode.AZU:
      return AZURE_REGIONS[regionCode]?.name ?? regionCode;
    case ProviderCode.GCP:
      return regionCode;
    case ProviderCode.KUBERNETES:
      return KUBERNETES_REGIONS[regionCode]?.name ?? regionCode;
    default:
      return regionCode;
  }
};

export const getZoneOptions = (providerCode: ProviderCode, regionCode: string) => {
  switch (providerCode) {
    case ProviderCode.AWS:
      return AWS_REGIONS[regionCode]?.zones ?? [];
    case ProviderCode.AZU:
      return AZURE_REGIONS[regionCode]?.zones ?? [];
    case ProviderCode.GCP:
      return GCP_REGIONS[regionCode]?.zones ?? [];
    default:
      return [];
  }
};

export const getOnPremLocationOption = (latitude: number, longitude: number) => {
  const locationName =
    Object.keys(ON_PREM_LOCATIONS).find(
      (name) =>
        ON_PREM_LOCATIONS[name].latitude === latitude &&
        ON_PREM_LOCATIONS[name].longitude === longitude
    ) ?? ON_PREM_UNLISTED_LOCATION;
  return { label: locationName, value: ON_PREM_LOCATIONS[locationName] };
};
