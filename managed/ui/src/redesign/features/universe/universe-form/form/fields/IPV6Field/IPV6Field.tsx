import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBHelper, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { IPV6_FIELD } from '../../../utils/constants';

interface IPV6FieldProps {
  disabled: boolean;
}

export const IPV6Field = ({ disabled }: IPV6FieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%" data-testid="IPV6Field-Container">
      <YBLabel dataTestId="IPV6Field-Label">{t('universeForm.advancedConfig.enableIPV6')}</YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={IPV6_FIELD}
          inputProps={{
            'data-testid': 'IPV6Field-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
        <YBHelper dataTestId="IPV6Field-Helper">
          {t('universeForm.advancedConfig.enableIPV6Helper')}
        </YBHelper>
      </Box>
    </Box>
  );
};

//shown only for k8s
