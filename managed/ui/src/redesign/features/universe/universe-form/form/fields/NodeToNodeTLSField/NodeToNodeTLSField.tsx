import { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBTooltip, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { NODE_TO_NODE_ENCRYPT_FIELD, ROOT_CA_CLIENT_CA_SAME_FIELD } from '../../../utils/constants';
import InfoMessageIcon from '../../../../../../assets/info-message.svg?img';

interface NodeToNodeTLSFieldProps {
  disabled: boolean;
}

export const NodeToNodeTLSField = ({ disabled }: NodeToNodeTLSFieldProps): ReactElement => {
  const { control, watch } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const nodeToNodeTooltipTitle = t(
    'universeForm.securityConfig.encryptionSettings.enableNodeToNodeTLSHelper'
  );

  const rootCaClientCaSame = watch(ROOT_CA_CLIENT_CA_SAME_FIELD);

  return (
    <Box display="flex" width="100%" data-testid="NodeToNodeTLSField-Container" mt={2}>
      <YBToggleField
        name={NODE_TO_NODE_ENCRYPT_FIELD}
        inputProps={{
          'data-testid': 'NodeToNodeTLSField-Toggle'
        }}
        control={control}
        disabled={disabled}
      />
      <Box flex={1} alignSelf="center">
        <YBLabel dataTestId="NodeToNodeTLSField-Label" width="fit-content">
          {
            rootCaClientCaSame ? t('universeForm.securityConfig.encryptionSettings.enableBothNodeAndClientTLS') : t('universeForm.securityConfig.encryptionSettings.enableNodeToNodeTLS')
          }
          &nbsp;
          <YBTooltip title={nodeToNodeTooltipTitle}>
            <img alt="Info" src={InfoMessageIcon} />
          </YBTooltip>
        </YBLabel>
      </Box>
    </Box>
  );
};

//shown only for aws, gcp, azu, on-pre, k8s
//disabled for non primary cluster
