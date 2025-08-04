import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { ArchitectureType } from '@app/redesign/features-v2/universe-form-wizard/helpers/constants';
import { CloudType, StorageType } from '@app/redesign/features/universe/universe-form/utils/dto';

export const InstanceSettingsValidationSchema = (
  t: TFunction,
  useK8CustomResources: boolean,
  provider: CloudType | undefined,
  useDedicatedNodes: boolean
) => {
  return Yup.object().shape({
    // CPU Architecture validation
    arch: Yup.string()
      .oneOf(
        Object.values(ArchitectureType),
        t('createUniverseV2.instanceSettings.validation.invalidArchitecture')
      )
      .required(
        t('createUniverseV2.instanceSettings.validation.required', { field: 'CPU Architecture' })
      ),

    // Image Bundle UUID validation
    imageBundleUUID: Yup.string()
      .nullable()
      .test(
        'image-bundle-required',
        t('createUniverseV2.instanceSettings.validation.required', { field: 'Linux Version' }),
        function (value) {
          // Image bundle is required when OS patching is enabled
          const { parent } = this;
          const osPatchingEnabled = parent?.osPatchingEnabled;

          if (osPatchingEnabled && provider && ['aws', 'gcp', 'azu'].includes(provider)) {
            return value !== null && value !== undefined && value !== '';
          }
          return true;
        }
      ),

    // Spot Instance validation
    useSpotInstance: Yup.boolean().required(
      t('createUniverseV2.instanceSettings.validation.required', { field: 'Spot Instance' })
    ),

    // Instance Type validation
    instanceType: Yup.string()
      .nullable()
      .test(
        'instance-type-required',
        t('createUniverseV2.instanceSettings.validation.required', { field: 'Instance Type' }),
        function (value) {
          const isK8s = provider === 'kubernetes';

          // Instance type is required for non-K8s or K8s without custom resources
          if (!isK8s || (isK8s && !useK8CustomResources)) {
            return value !== null && value !== undefined && value !== '';
          }
          return true;
        }
      ),

    // Master Instance Type validation
    masterInstanceType: Yup.string()
      .nullable()
      .test(
        'master-instance-type-required',
        t('createUniverseV2.instanceSettings.validation.required', {
          field: 'Master Instance Type'
        }),
        function (value) {
          const { parent } = this;
          const keepMasterTserverSame = parent?.keepMasterTserverSame;
          const isK8s = provider === 'kubernetes';

          // Master instance type is required when using dedicated nodes and not keeping master/tserver same
          if (useDedicatedNodes && !keepMasterTserverSame) {
            if (!isK8s || (isK8s && !useK8CustomResources)) {
              return value !== null && value !== undefined && value !== '';
            }
          }
          return true;
        }
      ),

    // Device Info validation
    deviceInfo: Yup.object()
      .nullable()
      .test(
        'device-info-required',
        t('createUniverseV2.instanceSettings.validation.required', { field: 'Device Info' }),
        function (value) {
          const isK8s = provider === 'kubernetes';

          // Device info is required for non-K8s or K8s without custom resources
          if (!isK8s || (isK8s && !useK8CustomResources)) {
            return value !== null && value !== undefined;
          }
          return true;
        }
      ),

    // Master Device Info validation
    masterDeviceInfo: Yup.object()
      .nullable()
      .test(
        'master-device-info-required',
        t('createUniverseV2.instanceSettings.validation.required', { field: 'Master Device Info' }),
        function (value) {
          const { parent } = this;
          const keepMasterTserverSame = parent?.keepMasterTserverSame;
          const isK8s = provider === 'kubernetes';

          // Master device info is required when using dedicated nodes and not keeping master/tserver same
          if (useDedicatedNodes && !keepMasterTserverSame) {
            if (!isK8s || (isK8s && !useK8CustomResources)) {
              return value !== null && value !== undefined;
            }
          }
          return true;
        }
      ),

    // K8S Node Resource Spec validation for TServer
    tserverK8SNodeResourceSpec: Yup.object()
      .nullable()
      .test(
        'tserver-k8s-node-spec-required',
        t('createUniverseV2.instanceSettings.validation.required', {
          field: 'TServer K8S Node Resource Spec'
        }),
        function (value) {
          const isK8s = provider === 'kubernetes';

          // K8S node spec is required for K8s with custom resources
          if (isK8s && useK8CustomResources) {
            return value !== null && value !== undefined;
          }
          return true;
        }
      ),

    // K8S Node Resource Spec validation for Master
    masterK8SNodeResourceSpec: Yup.object()
      .nullable()
      .test(
        'master-k8s-node-spec-required',
        t('createUniverseV2.instanceSettings.validation.required', {
          field: 'Master K8S Node Resource Spec'
        }),
        function (value) {
          const { parent } = this;
          const keepMasterTserverSame = parent?.keepMasterTserverSame;
          const isK8s = provider === 'kubernetes';

          // Master K8S node spec is required when using dedicated nodes and not keeping master/tserver same
          if (useDedicatedNodes && !keepMasterTserverSame && isK8s && useK8CustomResources) {
            return value !== null && value !== undefined;
          }
          return true;
        }
      ),

    // Keep Master TServer Same validation
    keepMasterTserverSame: Yup.boolean().nullable().default(false)
  });
};

// Additional validation schemas for nested objects
export const DeviceInfoValidationSchema = (t: TFunction) => {
  return Yup.object().shape({
    volumeSize: Yup.number()
      .positive(
        t('createUniverseV2.instanceSettings.validation.positiveNumber', { field: 'Volume Size' })
      )
      .required(
        t('createUniverseV2.instanceSettings.validation.required', { field: 'Volume Size' })
      )
      .min(
        1,
        t('createUniverseV2.instanceSettings.validation.minValue', { field: 'Volume Size', min: 1 })
      ),

    numVolumes: Yup.number()
      .positive(
        t('createUniverseV2.instanceSettings.validation.positiveNumber', {
          field: 'Number of Volumes'
        })
      )
      .required(
        t('createUniverseV2.instanceSettings.validation.required', { field: 'Number of Volumes' })
      )
      .min(
        1,
        t('createUniverseV2.instanceSettings.validation.minValue', {
          field: 'Number of Volumes',
          min: 1
        })
      ),

    diskIops: Yup.number()
      .nullable()
      .when('storageType', {
        is: StorageType.IO1,
        then: Yup.number()
          .positive(
            t('createUniverseV2.instanceSettings.validation.positiveNumber', { field: 'Disk IOPS' })
          )
          .required(
            t('createUniverseV2.instanceSettings.validation.required', { field: 'Disk IOPS' })
          )
          .min(
            100,
            t('createUniverseV2.instanceSettings.validation.minValue', {
              field: 'Disk IOPS',
              min: 100
            })
          )
      }),

    throughput: Yup.number()
      .nullable()
      .when('storageType', {
        is: StorageType.GP3,
        then: Yup.number()
          .positive(
            t('createUniverseV2.instanceSettings.validation.positiveNumber', {
              field: 'Throughput'
            })
          )
          .required(
            t('createUniverseV2.instanceSettings.validation.required', { field: 'Throughput' })
          )
      }),

    storageClass: Yup.string()
      .oneOf(['standard'], t('createUniverseV2.instanceSettings.validation.invalidStorageClass'))
      .required(
        t('createUniverseV2.instanceSettings.validation.required', { field: 'Storage Class' })
      ),

    mountPoints: Yup.string()
      .nullable()
      .matches(
        /^\/[a-zA-Z0-9/_-]+$/,
        t('createUniverseV2.instanceSettings.validation.invalidMountPoint')
      ),

    storageType: Yup.string()
      .nullable()
      .oneOf(
        Object.values(StorageType),
        t('createUniverseV2.instanceSettings.validation.invalidStorageType')
      )
  });
};

export const K8NodeSpecValidationSchema = (t: TFunction) => {
  return Yup.object().shape({
    memoryGib: Yup.number()
      .positive(
        t('createUniverseV2.instanceSettings.validation.positiveNumber', { field: 'Memory (GiB)' })
      )
      .required(
        t('createUniverseV2.instanceSettings.validation.required', { field: 'Memory (GiB)' })
      ),

    cpuCoreCount: Yup.number()
      .positive(
        t('createUniverseV2.instanceSettings.validation.positiveNumber', {
          field: 'CPU Core Count'
        })
      )
      .required(
        t('createUniverseV2.instanceSettings.validation.required', { field: 'CPU Core Count' })
      )
  });
};
