import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBHelper, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { NODE_TO_NODE_ENCRYPT_FIELD } from '../../../utils/constants';

interface NodeToNodeTLSFieldProps {
  disabled: boolean;
}

export const NodeToNodeTLSField = ({ disabled }: NodeToNodeTLSFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%" data-testid="NodeToNodeTLSField-Container">
      <YBLabel dataTestId="NodeToNodeTLSField-Label">
        {t('universeForm.instanceConfig.enableNodeToNodeTLS')}
      </YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={NODE_TO_NODE_ENCRYPT_FIELD}
          inputProps={{
            'data-testid': 'NodeToNodeTLSField-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
        <YBHelper dataTestId="NodeToNodeTLSField-Helper">
          {t('universeForm.instanceConfig.enableNodeToNodeTLSHelper')}
        </YBHelper>
      </Box>
    </Box>
  );
};

//shown only for aws, gcp, azu, on-pre, k8s
//disabled for non primary cluster
