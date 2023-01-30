import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBHelper, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { CLIENT_TO_NODE_ENCRYPT_FIELD } from '../../../utils/constants';

interface ClientToNodeTLSFieldProps {
  disabled: boolean;
}

export const ClientToNodeTLSField = ({ disabled }: ClientToNodeTLSFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%" data-testid="ClientToNodeTLSField-Container">
      <YBLabel dataTestId="ClientToNodeTLSField-Label">
        {t('universeForm.instanceConfig.enableClientToNodeTLS')}
      </YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={CLIENT_TO_NODE_ENCRYPT_FIELD}
          inputProps={{
            'data-testid': 'ClientToNodeTLSField-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
        <YBHelper dataTestId="ClientToNodeTLSField-Helper">
          {t('universeForm.instanceConfig.enableClientToNodeTLSHelper')}
        </YBHelper>
      </Box>
    </Box>
  );
};

//shown only for aws, gcp, azu, on-pre, k8s
//disabled for non primary cluster
