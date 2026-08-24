import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { AWS_ARN_STRING_FIELD } from '../FieldNames';

export const AWS_ARN_REGEX = /^arn:[a-z0-9-]+:[a-z0-9-]*:[a-z0-9-]*:[0-9]*:.+$/;

export const awsArnStringSchema = (t: TFunction) =>
  Yup.string()
    .transform((value) => (value == null ? '' : value))
    .matches(AWS_ARN_REGEX, {
      message: t('validation.invalidArn', { keyPrefix: 'createUniverseV2' }),
      excludeEmptyString: true
    });

export const awsArnFormSchema = (t: TFunction) =>
  Yup.object({
    [AWS_ARN_STRING_FIELD]: awsArnStringSchema(t)
  });
