/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { ON_PREM_LOCATIONS, ON_PREM_CUSTOM_LOCATION } from '../../providerRegionsData';
import { RegionMetadataResponse } from '../../types';

export const getRegionOption = (
  regionCode: string,
  regionMetadataResponse: RegionMetadataResponse
) => {
  const regionDetails = regionMetadataResponse.regionMetadata[regionCode];
  return {
    value: { code: regionCode, zoneOptions: regionDetails?.availabilityZones ?? [] },
    label: regionDetails?.name ?? regionCode
  };
};

export const getRegionOptions = (regionMetadataResponse: RegionMetadataResponse) =>
  regionMetadataResponse.regionMetadata
    ? Object.entries(regionMetadataResponse.regionMetadata).map(([regionCode, regionDetails]) => ({
        value: { code: regionCode, zoneOptions: regionDetails.availabilityZones ?? [] },
        label: regionDetails.name ?? regionCode
      }))
    : [];

export const getOnPremLocationOption = (latitude: number, longitude: number) => {
  const locationName =
    Object.keys(ON_PREM_LOCATIONS).find(
      (name) =>
        ON_PREM_LOCATIONS[name].latitude === latitude &&
        ON_PREM_LOCATIONS[name].longitude === longitude
    ) ?? ON_PREM_CUSTOM_LOCATION;
  return { label: locationName, value: locationName };
};
