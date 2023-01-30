import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBHelper, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { EAR_FIELD } from '../../../utils/constants';
interface EncryptionAtRestFieldProps {
  disabled: boolean;
}

export const EncryptionAtRestField = ({ disabled }: EncryptionAtRestFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%" data-testid="EncryptionAtRestField-Container">
      <YBLabel dataTestId="EncryptionAtRestField-Label">
        {t('universeForm.instanceConfig.enableEncryptionAtRest')}
      </YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={EAR_FIELD}
          inputProps={{
            'data-testid': 'EncryptionAtRestField-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
        <YBHelper dataTestId="EncryptionAtRestField-Helper">
          {t('universeForm.instanceConfig.enableEncryptionAtRestHelper')}
        </YBHelper>
      </Box>
    </Box>
  );
};

//shown only for aws, gcp, azu, on-pre, k8s
//disabled for non primary cluster
