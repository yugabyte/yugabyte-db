import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  DeviceInfoValidationSchema,
  K8VolumeInfoValidationSchema
} from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/ValidationSchema';

export const RRInstanceSettingsValidationSchema = (
  t: TFunction,
  useK8CustomResources: boolean,
  provider: CloudType | undefined
) => {
  const isK8s = provider === 'kubernetes';
  const requireInstanceFields = !isK8s || (isK8s && !useK8CustomResources);
  const volumeInfoSchema =
    isK8s && useK8CustomResources
      ? K8VolumeInfoValidationSchema(t)
      : DeviceInfoValidationSchema(t);

  return Yup.object().shape({
    imageBundleUUID: Yup.string()
      .nullable()
      .test(
        'image-bundle-required',
        t('validation.required', { field: 'Linux Version' }),
        function (value) {
          const osPatchingEnabled = Boolean(
            (this.options.context as { osPatchingEnabled?: boolean } | undefined)?.osPatchingEnabled
          );
          if (osPatchingEnabled && provider && ['aws', 'gcp', 'azu'].includes(provider)) {
            return value !== null && value !== undefined && value !== '';
          }
          return true;
        }
      ),

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

    deviceInfo: Yup.mixed().when('inheritPrimaryInstance', (inherit: boolean, schema) => {
      if (inherit) {
        return schema.nullable();
      }
      return volumeInfoSchema;
    }),

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
  });
};
