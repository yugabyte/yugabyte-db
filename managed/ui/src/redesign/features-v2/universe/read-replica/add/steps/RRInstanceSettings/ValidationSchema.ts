import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { DeviceInfoValidationSchema } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/ValidationSchema';

export const RRInstanceSettingsValidationSchema = (
  t: TFunction,
  useK8CustomResources: boolean,
  provider: CloudType | undefined
) => {
  const isK8s = provider === 'kubernetes';
  const requireInstanceFields = !isK8s || (isK8s && !useK8CustomResources);

  return Yup.object().shape({
    // Instance Type
    instanceType: Yup.string()
      .nullable()
      .test(
        'instance-type-required',
        t('validation.required', { field: 'Instance Type' }),
        function (value) {
          const { parent } = this;
          if (parent?.inheritPrimaryInstance) return true;
          if (!requireInstanceFields) return true;
          return value !== null && value !== undefined && value !== '';
        }
      ),

    // Device Info
    deviceInfo: Yup.object()
      .nullable()
      .test('device-info-required', t('validation.required', { field: 'Device Info' }), function (
        value
      ) {
        const { parent } = this;
        if (parent?.inheritPrimaryInstance) return true;
        if (!requireInstanceFields) return true;
        return value !== null && value !== undefined;
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

    // EBS Volume Encryption toggle
    enableEbsVolumeEncryption: Yup.boolean().nullable().default(false),

    // EBS KMS Config
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
