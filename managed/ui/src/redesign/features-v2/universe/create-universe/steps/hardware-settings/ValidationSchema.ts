import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { ArchitectureType } from '@app/redesign/features-v2/universe/create-universe/helpers/constants';
import { CloudType, StorageType } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  getDiskIopsRange,
  getThroughputRange
} from '@app/redesign/features-v2/universe/create-universe/fields/volume-info/VolumeInfoFieldHelper';

type InstanceSettingsValidationContext = {
  earKmsConfig?: string | null;
};

export const InstanceSettingsValidationSchema = (
  t: TFunction,
  useK8CustomResources: boolean,
  provider: CloudType | undefined,
  useDedicatedNodes: boolean
) => {
  const isK8s = provider === 'kubernetes';
  const requireTserverK8Spec = isK8s && useK8CustomResources;
  const volumeInfoSchema = requireTserverK8Spec
    ? K8VolumeInfoValidationSchema(t)
    : DeviceInfoValidationSchema(t);
  const masterHardwareShown = !!useDedicatedNodes || (isK8s && useK8CustomResources);
  const requiresSeparateMasterHardware = (keepSame: unknown) => {
    const same = Array.isArray(keepSame) ? keepSame[0] : keepSame;
    return masterHardwareShown && !same;
  };

  return Yup.object().shape({
    arch: Yup.string()
      .oneOf(Object.values(ArchitectureType), t('validation.invalidArchitecture'))
      .required(t('validation.required', { field: 'CPU Architecture' })),

    imageBundleUUID: Yup.string()
      .nullable()
      .test(
        'image-bundle-required',
        t('validation.required', { field: 'Linux Version' }),
        function (value) {
          const { parent } = this;
          const osPatchingEnabled = parent?.osPatchingEnabled;

          if (osPatchingEnabled && provider && ['aws', 'gcp', 'azu'].includes(provider)) {
            return value !== null && value !== undefined && value !== '';
          }
          return true;
        }
      ),

    useSpotInstance: Yup.boolean().required(t('validation.required', { field: 'Spot Instance' })),

    instanceType: Yup.string()
      .nullable()
      .test(
        'instance-type-required',
        t('validation.required', { field: 'Instance Type' }),
        function (value) {
          if (!isK8s || (isK8s && !useK8CustomResources)) {
            return value !== null && value !== undefined && value !== '';
          }
          return true;
        }
      ),

    masterInstanceType: Yup.string()
      .nullable()
      .test(
        'master-instance-type-required',
        t('validation.required', {
          field: 'Master Instance Type'
        }),
        function (value) {
          const { parent } = this;
          if (!requiresSeparateMasterHardware(parent?.keepMasterTserverSame)) {
            return true;
          }
          if (!isK8s || (isK8s && !useK8CustomResources)) {
            return value !== null && value !== undefined && value !== '';
          }
          return true;
        }
      ),

    deviceInfo: volumeInfoSchema,

    masterDeviceInfo: Yup.mixed().when('keepMasterTserverSame', {
      is: (keepSame: unknown) => requiresSeparateMasterHardware(keepSame),
      then: volumeInfoSchema,
      otherwise: Yup.mixed().nullable()
    }),

    tserverK8SNodeResourceSpec: requireTserverK8Spec
      ? K8NodeSpecValidationSchema(t)
      : Yup.mixed().nullable(),

    masterK8SNodeResourceSpec: Yup.mixed().when('keepMasterTserverSame', {
      is: (keepSame: unknown) => requireTserverK8Spec && requiresSeparateMasterHardware(keepSame),
      then: K8NodeSpecValidationSchema(t),
      otherwise: Yup.mixed().nullable()
    }),

    keepMasterTserverSame: Yup.boolean().nullable().default(false),

    enableEbsVolumeEncryption: Yup.boolean().nullable().default(false),

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
      .test('ebs-kms-config-duplicate', t('kmsValidationMsg'), function (value) {
        const earKmsConfig = (this.options.context as InstanceSettingsValidationContext | undefined)
          ?.earKmsConfig;
        if (!value || !earKmsConfig) return true;
        return value !== earKmsConfig;
      })
  });
};

const isEmptyNumberInput = (value: unknown) =>
  value === null || value === undefined || value === '';

const toFiniteNumber = (value: unknown): number | null => {
  if (typeof value === 'number') return Number.isFinite(value) ? value : null;
  if (typeof value === 'string' && value.trim() !== '') {
    const n = Number(value);
    return Number.isFinite(n) ? n : null;
  }
  return null;
};

// yup number().nullable().required() can let null through
const requiredPositiveNumber = (t: TFunction, field: string, min = 1) =>
  Yup.mixed()
    .test('required', t('validation.required', { field }), (value) => {
      return !isEmptyNumberInput(value);
    })
    .test('positive', t('validation.positiveNumber', { field }), (value) => {
      if (isEmptyNumberInput(value)) return true;
      const n = toFiniteNumber(value);
      return n != null && n > 0;
    })
    .test('min', t('validation.minValue', { field, min }), (value) => {
      if (isEmptyNumberInput(value)) return true;
      const n = toFiniteNumber(value);
      if (n == null || n <= 0) return true;
      return n >= min;
    });

export const DeviceInfoValidationSchema = (t: TFunction) => {
  return Yup.object().shape({
    volumeSize: requiredPositiveNumber(t, 'Volume Size', 1),
    numVolumes: requiredPositiveNumber(t, 'Number of Volumes', 1),

    diskIops: Yup.mixed()
      .nullable()
      .test(
        'disk-iops-required',
        t('validation.required', { field: 'Disk IOPS' }),
        function (value) {
          if (!getDiskIopsRange(this.parent?.storageType)) return true;
          return !isEmptyNumberInput(value);
        }
      )
      .test('disk-iops-range', function (value) {
        const range = getDiskIopsRange(this.parent?.storageType);
        if (!range || isEmptyNumberInput(value)) return true;
        const n = toFiniteNumber(value);
        if (n == null) {
          return this.createError({
            message: t('validation.required', { field: 'Disk IOPS' })
          });
        }
        if (n <= 0) {
          return this.createError({
            message: t('validation.positiveNumber', { field: 'Disk IOPS' })
          });
        }
        if (n < range.min) {
          return this.createError({
            message: t('validation.minValue', { field: 'Disk IOPS', min: range.min })
          });
        }
        if (n > range.max) {
          return this.createError({
            message: t('validation.maxValue', { field: 'Disk IOPS', max: range.max })
          });
        }
        return true;
      }),

    throughput: Yup.mixed()
      .nullable()
      .test(
        'throughput-required',
        t('validation.required', { field: 'Throughput' }),
        function (value) {
          if (!getThroughputRange(this.parent?.storageType)) return true;
          return !isEmptyNumberInput(value);
        }
      )
      .test('throughput-range', function (value) {
        const range = getThroughputRange(this.parent?.storageType);
        if (!range || isEmptyNumberInput(value)) return true;
        const n = toFiniteNumber(value);
        if (n == null) {
          return this.createError({
            message: t('validation.required', { field: 'Throughput' })
          });
        }
        if (n <= 0) {
          return this.createError({
            message: t('validation.positiveNumber', { field: 'Throughput' })
          });
        }
        if (n < range.min) {
          return this.createError({
            message: t('validation.minValue', { field: 'Throughput', min: range.min })
          });
        }
        if (n > range.max) {
          return this.createError({
            message: t('validation.maxValue', { field: 'Throughput', max: range.max })
          });
        }
        return true;
      }),

    storageClass: Yup.string()
      .nullable()
      .test(
        'storage-class-required',
        t('validation.required', { field: 'Storage Class' }),
        (value) => value !== null && value !== undefined && String(value).trim() !== ''
      ),

    mountPoints: Yup.string()
      .nullable()
      .test(
        'mount-point',
        t('validation.invalidMountPoint'),
        (value) =>
          value === null || value === undefined || value === '' || /^\/[a-zA-Z0-9/_-]+$/.test(value)
      ),

    // null ok for k8 / ephemeral
    storageType: Yup.string()
      .nullable()
      .test(
        'storage-type',
        t('validation.invalidStorageType'),
        (value) =>
          value === null ||
          value === undefined ||
          value === '' ||
          Object.values(StorageType).includes(value as StorageType)
      )
  });
};

export const K8VolumeInfoValidationSchema = (t: TFunction) => {
  return Yup.object().shape({
    volumeSize: requiredPositiveNumber(t, 'Volume Size', 1),
    numVolumes: requiredPositiveNumber(t, 'Number of Volumes', 1),
    storageClass: Yup.string()
      .nullable()
      .test(
        'storage-class-required',
        t('validation.required', { field: 'Storage Class' }),
        (value) => value !== null && value !== undefined && String(value).trim() !== ''
      )
  });
};

export const K8NodeSpecValidationSchema = (t: TFunction) => {
  return Yup.object().shape({
    memoryGib: requiredPositiveNumber(t, 'Memory (GiB)', 0.01),
    cpuCoreCount: requiredPositiveNumber(t, 'CPU Core Count', 0.01)
  });
};
