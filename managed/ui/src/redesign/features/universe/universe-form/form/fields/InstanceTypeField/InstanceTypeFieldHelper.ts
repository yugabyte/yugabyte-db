import _ from 'lodash';
import { CloudType, InstanceType, InstanceTypeWithGroup, RunTimeConfigEntry } from '../../../utils/dto';

const INSTANCE_WITH_EPHEMERAL_STORAGE_ONLY = ['i3', 'c5d', 'c6gd'];

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
  } else if(providerCode === CloudType.gcp) {
    instanceType = runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.gcp.default_instance_type'
    )?.value;
  } else if(providerCode === CloudType.kubernetes) {
    instanceType = runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.default_instance_type'
    )?.value;
  } else if (providerCode === CloudType.azu) {
    instanceType = runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === 'yb.azure.default_instance_type'
    )?.value;
  }
  return instanceType;
}

export const canUseSpotInstance = (runtimeConfigs: any) => {
  return runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.use_spot_instances'
  )?.value === 'true';
}

export const isEphemeralAwsStorageInstance = (instance: InstanceType) => {
  return (
    instance.providerCode === CloudType.aws &&
    INSTANCE_WITH_EPHEMERAL_STORAGE_ONLY.includes(instance.instanceTypeCode?.split?.('.')[0])
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
