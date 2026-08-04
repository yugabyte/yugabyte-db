import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { ArchitectureType } from '@app/redesign/features-v2/universe/create-universe/helpers/constants';
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
      .oneOf(Object.values(ArchitectureType), t('validation.invalidArchitecture'))
      .required(t('validation.required', { field: 'CPU Architecture' })),

    // Image Bundle UUID validation
    imageBundleUUID: Yup.string()
      .nullable()
      .test(
        'image-bundle-required',
        t('validation.required', { field: 'Linux Version' }),
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
    useSpotInstance: Yup.boolean().required(t('validation.required', { field: 'Spot Instance' })),

    // Instance Type validation
    instanceType: Yup.string()
      .nullable()
      .test(
        'instance-type-required',
        t('validation.required', { field: 'Instance Type' }),
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
        t('validation.required', {
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
      .test('device-info-required', t('validation.required', { field: 'Device Info' }), function (
        value
      ) {
        const isK8s = provider === 'kubernetes';

        // Device info is required for non-K8s or K8s without custom resources
        if (!isK8s || (isK8s && !useK8CustomResources)) {
          return value !== null && value !== undefined;
        }
        return true;
      })
      .test('device-info-shape', t('validation.required', { field: 'Device Info' }), function (
        value
      ) {
        if (value == null) return true;
        try {
          DeviceInfoValidationSchema(t).validateSync(value, { abortEarly: false });
          return true;
        } catch (err: any) {
          const message =
            err?.errors?.[0] ?? err?.message ?? t('validation.required', { field: 'Device Info' });
          return this.createError({ message });
        }
      }),

    // Master Device Info validation - required + shape (same as deviceInfo)
    masterDeviceInfo: Yup.object()
      .nullable()
      .test(
        'master-device-info-required',
        t('validation.required', { field: 'Master Device Info' }),
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
      )
      .test(
        'device-info-shape',
        t('validation.required', { field: 'Master Device Info' }),
        function (value) {
          if (value == null) return true;
          try {
            DeviceInfoValidationSchema(t).validateSync(value, { abortEarly: false });
            return true;
          } catch (err: any) {
            const message =
              err?.errors?.[0] ??
              err?.message ??
              t('validation.required', { field: 'Master Device Info' });
            return this.createError({ message });
          }
        }
      ),

    // K8S Node Resource Spec validation for TServer
    tserverK8SNodeResourceSpec: Yup.object()
      .nullable()
      .test(
        'tserver-k8s-node-spec-required',
        t('validation.required', {
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
        t('validation.required', {
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
    keepMasterTserverSame: Yup.boolean().nullable().default(false),

    // EBS Volume Encryption toggle (optional)
    enableEbsVolumeEncryption: Yup.boolean().nullable().default(false),

    // EBS KMS Config - required when EBS volume encryption is enabled
    ebsKmsConfigUUID: Yup.string()
      .nullable()
      .test(
        'ebs-kms-config-required',
        t('validation.required', {
          field: t('kmsConfig')
        }),
        function (value) {
          const { parent } = this;
          const enableEbsVolumeEncryption = parent?.enableEbsVolumeEncryption;

          if (enableEbsVolumeEncryption) {
            return value !== null && value !== undefined && value !== '';
          }
          return true;
        }
      )
  });
};

// Additional validation schemas for nested objects
export const DeviceInfoValidationSchema = (t: TFunction) => {
  return Yup.object().shape({
    volumeSize: Yup.number()
      .positive(t('validation.positiveNumber', { field: 'Volume Size' }))
      .required(t('validation.required', { field: 'Volume Size' }))
      .min(1, t('validation.minValue', { field: 'Volume Size', min: 1 })),

    numVolumes: Yup.number()
      .positive(
        t('validation.positiveNumber', {
          field: 'Number of Volumes'
        })
      )
      .required(t('validation.required', { field: 'Number of Volumes' }))
      .min(
        1,
        t('validation.minValue', {
          field: 'Number of Volumes',
          min: 1
        })
      ),

    diskIops: Yup.number()
      .nullable()
      .when('storageType', {
        is: StorageType.IO1,
        then: Yup.number()
          .positive(t('validation.positiveNumber', { field: 'Disk IOPS' }))
          .required(t('validation.required', { field: 'Disk IOPS' }))
          .min(
            100,
            t('validation.minValue', {
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
            t('validation.positiveNumber', {
              field: 'Throughput'
            })
          )
          .required(t('validation.required', { field: 'Throughput' }))
      }),

    storageClass: Yup.string()
      .oneOf(['standard'], t('validation.invalidStorageClass'))
      .required(t('validation.required', { field: 'Storage Class' })),

    mountPoints: Yup.string()
      .nullable()
      .matches(/^\/[a-zA-Z0-9/_-]+$/, t('validation.invalidMountPoint')),

    storageType: Yup.string()
      .nullable()
      .oneOf(Object.values(StorageType), t('validation.invalidStorageType'))
  });
};

export const K8NodeSpecValidationSchema = (t: TFunction) => {
  return Yup.object().shape({
    memoryGib: Yup.number()
      .positive(t('validation.positiveNumber', { field: 'Memory (GiB)' }))
      .required(t('validation.required', { field: 'Memory (GiB)' })),

    cpuCoreCount: Yup.number()
      .positive(
        t('validation.positiveNumber', {
          field: 'CPU Core Count'
        })
      )
      .required(t('validation.required', { field: 'CPU Core Count' }))
  });
};
