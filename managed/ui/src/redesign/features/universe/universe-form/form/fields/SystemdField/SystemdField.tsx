import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { SYSTEMD_FIELD } from '../../../utils/constants';

interface SystemDFieldProps {
  disabled: boolean;
}

export const SystemDField = ({ disabled }: SystemDFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%" data-testid="SystemDField-Container">
      <YBLabel dataTestId="SystemDField-Label">
        {t('universeForm.advancedConfig.enableSystemD')}
      </YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={SYSTEMD_FIELD}
          inputProps={{
            'data-testid': 'SystemDField-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
      </Box>
    </Box>
  );
};

//shown only after provider is selected
//show for non k8s
