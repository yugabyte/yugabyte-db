import _ from 'lodash';
import {
  CloudType,
  InstanceType,
  InstanceTypeWithGroup,
  Placement,
  RunTimeConfigEntry,
  Region
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { useEffect, useState } from 'react';
import { useQuery } from 'react-query';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import { ProviderType } from '../../steps/general-settings/dtos';

const INSTANCE_WITH_EPHEMERAL_STORAGE_ONLY = [
  'g5',
  'g6',
  'g6e',
  'gr6',
  'i3',
  'i3en',
  'i4g',
  'i4i',
  'im4gn',
  'is4gen',
  'p5',
  'p5e',
  'trn1',
  'trn1n',
  'x1',
  'x1e'
];

export const AZURE_INSTANCE_TYPE_GROUPS = {
  'B-Series': /^standard_b.+/i,
  'D-Series': /^standard_d.+/i,
  'E-Series': /^standard_e.+/i,
  'F-Series': /^standard_f.+/i,
  'GS-Series': /^standard_gs.+/i,
  'H-Series': /^standard_h.+/i,
  'L-Series': /^standard_l.+/i,
  'M-Series': /^standard_m.+/i,
  'N-Series': /^standard_n.+/i,
  'P-Series': /^standard_p.+/i
};

export const getDefaultInstanceType = (providerCode: string, runtimeConfigs: any) => {
  let instanceType = null;

  if (providerCode === CloudType.aws) {
    instanceType = runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.aws.default_instance_type'
    )?.value;
  } else if (providerCode === CloudType.gcp) {
    instanceType = runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.gcp.default_instance_type'
    )?.value;
  } else if (providerCode === CloudType.kubernetes) {
    instanceType = runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.default_instance_type'
    )?.value;
  } else if (providerCode === CloudType.azu) {
    instanceType = runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.azure.default_instance_type'
    )?.value;
  }
  return instanceType;
};

export const canUseSpotInstance = (runtimeConfigs: any) => {
  return (
    runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.use_spot_instances'
    )?.value === 'true'
  );
};

export const isEphemeralAwsStorageInstance = (instance: InstanceType) => {
  return (
    instance.providerCode === CloudType.aws &&
    (INSTANCE_WITH_EPHEMERAL_STORAGE_ONLY.includes(instance.instanceTypeCode?.split?.('.')[0]) ||
      instance.instanceTypeCode?.split?.('.')[0].includes('d'))
  );
};

export const sortAndGroup = (data?: InstanceType[], cloud?: CloudType): InstanceTypeWithGroup[] => {
  if (!data) return [];

  const getGroupName = (instanceTypeCode: string): string => {
    switch (cloud) {
      case CloudType.aws:
        return instanceTypeCode.split('.')[0]; // c5.large --> c5
      case CloudType.gcp:
        return instanceTypeCode.split('-')[0]; // n1-standard-1 --> n1
      case CloudType.azu:
        for (const [groupName, regexp] of Object.entries(AZURE_INSTANCE_TYPE_GROUPS)) {
          if (regexp.test(instanceTypeCode)) return groupName;
        }
        return 'Other';
      default:
        return '';
    }
  };

  // add categories
  const result: InstanceTypeWithGroup[] = data.map((item) => {
    const groupName = getGroupName(item.instanceTypeCode);
    return {
      ...item,
      groupName: [groupName, 'type instances'].join(' ')
    };
  });

  // sort by group names and label
  return _.sortBy(result, ['groupName', 'label', 'numCores']);
};

export const useGetZones = (provider?: Partial<ProviderType>, regionList?: Region[]) => {
  const [zones, setZones] = useState<Placement[]>([]);

  const { data: allRegions, isLoading: isLoadingZones } = useQuery(
    [QUERY_KEY.getRegionsList, provider?.uuid],
    () => api.getRegionsList(provider?.uuid),
    { enabled: !!provider?.uuid } // make sure query won't run when there's no provider defined
  );
  useEffect(() => {
    const selectedRegionUUIDs = new Set((regionList ?? []).map((r: Region) => r.uuid ?? r));

    const zones = (allRegions || [])
      .filter((region) => selectedRegionUUIDs.has(region.uuid))
      .flatMap<Placement>((region: any) => {
        // add extra fields with parent region data
        return region.zones.map((zone: any) => ({
          ...zone,
          parentRegionId: region.uuid,
          parentRegionName: region.name,
          parentRegionCode: region.code
        }));
      });

    setZones(_.sortBy(zones, 'name'));
  }, [allRegions, regionList]);

  return { zones, isLoadingZones };
};
