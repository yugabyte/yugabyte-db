import { ReactElement } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBRadioGroupField, YBToggleField, YBLabel } from '../../../../../../components';
import { UniverseFormData, K8sEncryptionOption } from '../../../utils/dto';
import {
  K8S_ENCRYPTION_TYPE_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  ROOT_CA_CLIENT_CA_SAME_FIELD,
  ROOT_CERT_FIELD,
  CLIENT_CERT_FIELD,
  ENABLE_TLS_FIELD
} from '../../../utils/constants';

interface K8EncryptionTypeProps {
  disabled?: boolean;
}

export const K8EncryptionTypeField = ({ disabled }: K8EncryptionTypeProps): ReactElement => {
  const { control, watch, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  const K8S_ENCRYPTION_OPTIONS = [
    {
      value: K8sEncryptionOption.EnableBoth,
      label: t('universeActions.encryptionInTransit.enableCNAndNNEncryption')
    },
    {
      value: K8sEncryptionOption.NodeToNode,
      label: t('universeActions.encryptionInTransit.enableNNEncryption')
    },
    {
      value: K8sEncryptionOption.ClienToNode,
      label: t('universeActions.encryptionInTransit.enableCNEncryption')
    }
  ];

  const k8sEncryption = watch(K8S_ENCRYPTION_TYPE_FIELD);
  const rootCertVal = watch(ROOT_CERT_FIELD);
  const clientCertVal = watch(CLIENT_CERT_FIELD);
  const enableTLSVal = watch(ENABLE_TLS_FIELD);

  const updateStates = (encryptionType: K8sEncryptionOption) => {
    if (encryptionType === K8sEncryptionOption.EnableBoth) {
      setValue(NODE_TO_NODE_ENCRYPT_FIELD, true);
      setValue(CLIENT_TO_NODE_ENCRYPT_FIELD, true);
      setValue(ROOT_CA_CLIENT_CA_SAME_FIELD, true);
      setValue(ROOT_CERT_FIELD, rootCertVal ? rootCertVal : clientCertVal);
      setValue(CLIENT_CERT_FIELD, rootCertVal ? rootCertVal : clientCertVal);
    }
    if (encryptionType === K8sEncryptionOption.NodeToNode) {
      setValue(NODE_TO_NODE_ENCRYPT_FIELD, true);
      setValue(CLIENT_TO_NODE_ENCRYPT_FIELD, false);
      setValue(ROOT_CA_CLIENT_CA_SAME_FIELD, false);
      setValue(ROOT_CERT_FIELD, rootCertVal ? rootCertVal : clientCertVal);
      setValue(CLIENT_CERT_FIELD, '');
    }
    if (encryptionType === K8sEncryptionOption.ClienToNode) {
      setValue(NODE_TO_NODE_ENCRYPT_FIELD, false);
      setValue(CLIENT_TO_NODE_ENCRYPT_FIELD, true);
      setValue(ROOT_CA_CLIENT_CA_SAME_FIELD, false);
      setValue(CLIENT_CERT_FIELD, rootCertVal ? rootCertVal : clientCertVal);
      setValue(ROOT_CERT_FIELD, '');
    }
  };

  useUpdateEffect(() => {
    if (k8sEncryption) updateStates(k8sEncryption);
  }, [k8sEncryption]);

  useUpdateEffect(() => {
    if (!enableTLSVal) {
      setValue(NODE_TO_NODE_ENCRYPT_FIELD, false);
      setValue(CLIENT_TO_NODE_ENCRYPT_FIELD, false);
      setValue(CLIENT_CERT_FIELD, '');
      setValue(ROOT_CERT_FIELD, '');
    } else updateStates(k8sEncryption ? k8sEncryption : K8sEncryptionOption.EnableBoth);
  }, [enableTLSVal]);

  return (
    <Box
      display="flex"
      width="100%"
      data-testid="K8EncryptionTypeField-Container"
      flexDirection={'column'}
    >
      <YBLabel dataTestId="EncryptionInTransit-Label">
        {t('universeForm.securityConfig.encryptionSettings.encryptionInTransit')}
      </YBLabel>
      <Box display="flex" flexDirection="row" mb={2} mt={2}>
        <YBToggleField
          name={ENABLE_TLS_FIELD}
          inputProps={{
            'data-testid': 'EncryptionAtRestField-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
        <YBLabel dataTestId="EncryptionAtRestField-Label" width="185px">
          {t('universeForm.securityConfig.encryptionSettings.enableEncryptionInTransit')}
        </YBLabel>
      </Box>
      {enableTLSVal && (
        <YBRadioGroupField
          name={K8S_ENCRYPTION_TYPE_FIELD}
          options={K8S_ENCRYPTION_OPTIONS}
          control={control}
          orientation="vertical"
          style={{ gap: 8 }}
          isDisabled={disabled}
        />
      )}
    </Box>
  );
};
