import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBHelper, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { DEDICATED_NODES_FIELD } from '../../../utils/constants';

interface DedicatedNodesFieldProps {
  disabled?: boolean;
}

export const DedicatedNodesField = ({ disabled }: DedicatedNodesFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%" data-testid="DedicatedNodesField-Container">
      <YBLabel dataTestId="DedicatedNodesField-Label">
        {t('universeForm.instanceConfig.dedicatedNodes')}
      </YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={DEDICATED_NODES_FIELD}
          inputProps={{
            'data-testid': 'DedicatedNodesField-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
        <YBHelper dataTestId="DedicatedNodesField-Helper">
          {t('universeForm.instanceConfig.dedicatedNodesHelper')}
        </YBHelper>
      </Box>
    </Box>
  );
};
