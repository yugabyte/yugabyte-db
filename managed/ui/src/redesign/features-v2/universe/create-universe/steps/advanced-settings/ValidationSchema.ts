import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { OtherAdvancedProps } from './dtos';
import { CloudType } from '@app/redesign/helpers/dtos';
import { canOverrideCommunicationPorts } from '../../helpers/syncConnectionPoolingPorts';
import {
  COMMUNICATION_PORT_FIELD_NAMES,
  findDuplicatePortFieldNames,
  throwDuplicatePortsYupError
} from '../../helpers/duplicatePortValidation';
import { getCommunicationPortFieldIds } from '../../utils/createUniversePayload';
import { awsArnStringSchema } from '../../fields/arn-field/awsArnValidation';

export interface OtherAdvancedValidationOptions {
  providerCode?: CloudType;
  requireAccessKey?: boolean;
  ysql?: boolean;
  ycql?: boolean;
  enableConnectionPooling?: boolean;
}

export const OtherAdvancedValidationSchema = (
  t: TFunction,
  options: OtherAdvancedValidationOptions = {}
) => {
  const {
    providerCode,
    requireAccessKey = providerCode !== CloudType.kubernetes,
    ysql,
    ycql,
    enableConnectionPooling
  } = options;

  const portShape = COMMUNICATION_PORT_FIELD_NAMES.reduce(
    (shape, fieldName) => {
      shape[fieldName] = Yup.mixed();
      return shape;
    },
    {} as Record<string, ReturnType<typeof Yup.mixed>>
  );

  return Yup.object<Partial<OtherAdvancedProps>>({
    accessKeyCode: requireAccessKey
      ? Yup.string().required(t('accessKeyRequired'))
      : Yup.string().notRequired(),
    awsArnString: awsArnStringSchema(t),
    ...portShape
  }).test('unique-deployment-ports', '', function (value) {
    if (!canOverrideCommunicationPorts(providerCode)) return true;

    const fieldNames = getCommunicationPortFieldIds(
      ysql,
      ycql,
      providerCode ?? '',
      enableConnectionPooling
    );
    const duplicateFields = findDuplicatePortFieldNames(
      (value ?? {}) as Record<string, unknown>,
      fieldNames
    );

    return throwDuplicatePortsYupError(
      duplicateFields,
      t('validation.duplicatePorts'),
      (params) => this.createError(params),
      this.path
    );
  });
};
