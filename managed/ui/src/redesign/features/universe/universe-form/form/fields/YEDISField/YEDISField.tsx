import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBHelper, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { YEDIS_FIELD } from '../../../utils/constants';
interface YEDISFieldProps {
  disabled: boolean;
}

export const YEDISField = ({ disabled }: YEDISFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%" data-testid="YEDISField-Container">
      <YBLabel dataTestId="YEDISField-Label">
        {t('universeForm.instanceConfig.enableYEDIS')}
      </YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={YEDIS_FIELD}
          inputProps={{
            'data-testid': 'YEDISField-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
        <YBHelper dataTestId="YEDISField-Helper">
          {t('universeForm.instanceConfig.enableYEDISHelper')}
        </YBHelper>
      </Box>
    </Box>
  );
};

//shown only for aws, gcp, azu, on-pre, k8s
//disabled for non primary cluster
