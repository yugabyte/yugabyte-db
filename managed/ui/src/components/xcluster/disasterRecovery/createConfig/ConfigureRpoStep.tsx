import { Box, FormHelperText, useTheme } from '@material-ui/core';
import { Field, FormikProps } from 'formik';
import { useTranslation } from 'react-i18next';

import { YBFormSelect, YBNumericInput } from '../../../common/forms/fields';
import { ReactSelectOption } from '../../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { RpoUnit } from '../constants';

import { CreateDrConfigFormValues } from './CreateConfigModal';

interface ConfigureRpoStepProps {
  formik: React.MutableRefObject<FormikProps<CreateDrConfigFormValues>>;
}

const RPO_DURATION_SELECT_STYLES = {
  container: (baseStyles: any) => ({
    ...baseStyles,
    width: 150
  })
};
const TRANSLATION_KEY_PREFIX =
  'clusterDetail.disasterRecovery.config.createModal.step.configureRpo';
export const ConfigureRpoStep = ({ formik }: ConfigureRpoStepProps) => {
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const { values, errors, setFieldValue } = formik.current;

  const RPO_UNIT_OPTIONS: ReactSelectOption[] = [
    { label: t('duration.second'), value: RpoUnit.SECOND },
    { label: t('duration.minute'), value: RpoUnit.MINUTE },
    { label: t('duration.hour'), value: RpoUnit.HOUR }
  ];

  return (
    <>
      <div>{t('instruction')}</div>
      <Box display="flex" gridGap={theme.spacing(1)}>
        <Field
          name="rpo"
          component={YBNumericInput}
          input={{
            onChange: (value: number) => setFieldValue('rpo', value),
            value: values.rpo
          }}
          minVal={1}
        />
        <Field
          name="rpoUnit"
          component={YBFormSelect}
          options={RPO_UNIT_OPTIONS}
          field={{
            name: 'rpoUnit',
            value: values.rpoUnit
          }}
          styles={RPO_DURATION_SELECT_STYLES}
        />
      </Box>
      {errors.rpo && <FormHelperText error={true}>{errors.rpo}</FormHelperText>}
    </>
  );
};
