import * as Yup from 'yup';
import { TFunction } from 'i18next';

export const RRRegionsAndAZValidationSchema = (t: TFunction) =>
  Yup.object({
    regions: Yup.array()
      .of(
        Yup.object({
          isNew: Yup.boolean().optional(),
          regionUuid: Yup.string()
            .nullable()
            .test('regionRequired', t('validation.regionRequired'), (v) => v !== null && v !== ''),
          zones: Yup.array()
            .of(
              Yup.object({
                zoneUuid: Yup.string()
                  .nullable()
                  .test('zoneRequired', t('validation.zoneRequired'), (v) => v !== null && v !== ''),
                nodeCount: Yup.number()
                  .typeError(t('validation.nodesPositiveInteger'))
                  .integer(t('validation.nodesPositiveInteger'))
                  .min(1, t('validation.minValue', { field: t('nodes'), min: 1 }))
                  .max(200, t('validation.maxValue', { field: t('nodes'), max: 200 }))
                  .required(t('validation.nodesPositiveInteger')),
                dataCopies: Yup.number().nullable().optional()
              })
            )
            .min(1)
        })
      )
      .min(1)
  });
