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
import { ACCESS_KEY_CODE_BY_PROVIDER } from '../../fields/FieldNames';

export interface OtherAdvancedValidationOptions {
  providerCode?: CloudType;
  /** Provider UUIDs that have ≥1 access key and must have a selection. */
  providersRequiringAccessKey?: string[];
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
    providersRequiringAccessKey = [],
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
    // Free-form Record — use mixed so Yup does not strip provider UUID keys.
    [ACCESS_KEY_CODE_BY_PROVIDER]: Yup.mixed<Record<string, string>>()
      .default({})
      .test('access-keys-required', '', function (value) {
        const map = (value ?? {}) as Record<string, string>;
        for (const uuid of providersRequiringAccessKey) {
          if (!map[uuid]) {
            return this.createError({
              path: `${this.path}.${uuid}`,
              message: t('accessKeyRequired')
            });
          }
        }
        return true;
      }),
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
