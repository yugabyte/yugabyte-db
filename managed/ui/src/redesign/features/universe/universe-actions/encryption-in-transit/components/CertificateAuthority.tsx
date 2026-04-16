import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useUpdateEffect } from 'react-use';
import { useFormContext } from 'react-hook-form';
import { Box, Divider, Typography } from '@material-ui/core';
import {
  AlertVariant,
  YBAlert,
  YBCheckboxField,
  YBToggleField,
  YBTooltip,
  YBRadioGroupField
} from '../../../../../components';
import { CertificateField } from '../components/CertificateField';
import {
  ENABLE_NODE_NODE_ENCRYPTION_NAME,
  NODE_NODE_CERT_FIELD_NAME,
  CLIENT_NODE_CERT_FIELD_NAME,
  ENABLE_CLIENT_NODE_ENCRYPTION_NAME,
  ROTATE_NODE_NODE_CERT_FIELD_NAME,
  ROTATE_CLIENT_NODE_CERT_FIELD_NAME,
  USE_SAME_CERTS_FIELD_NAME,
  K8S_ENCRYPTION_TYPE_FIELD,
  CertTypes,
  EncryptionInTransitFormValues,
  K8sEncryptionOption,
  useEITStyles
} from '../EncryptionInTransitUtils';

interface CertificateAuthorityProps {
  initialValues: EncryptionInTransitFormValues;
  isItKubernetesUniverse: boolean;
}

export const CertificateAuthority: FC<CertificateAuthorityProps> = ({
  initialValues,
  isItKubernetesUniverse
}) => {
  const { t } = useTranslation();
  const classes = useEITStyles();
  const { control, watch, getValues, setValue } = useFormContext<EncryptionInTransitFormValues>();

  //Initial Form value
  const encryptionEnabled = initialValues.enableUniverseEncryption;
  const clientRootCAInitial = initialValues.clientRootCA;
  const rootCAInitial = initialValues.rootCA;
  const enableNodeToNodeEncryptInitial = initialValues.enableNodeToNodeEncrypt;
  const enableClientToNodeEncryptInitial = initialValues.enableClientToNodeEncrypt;
  const k8sEncryptionTypeInital = initialValues.k8sEncryptionType;

  //Current form value
  const enableNNEncryption = watch(ENABLE_NODE_NODE_ENCRYPTION_NAME);
  const rootCA = watch(NODE_NODE_CERT_FIELD_NAME);
  const enableCNEncryption = watch(ENABLE_CLIENT_NODE_ENCRYPTION_NAME);
  const clientRootCA = watch(CLIENT_NODE_CERT_FIELD_NAME);
  const rootAndClientRootCASame = watch(USE_SAME_CERTS_FIELD_NAME);
  const rotateNToN = watch(ROTATE_NODE_NODE_CERT_FIELD_NAME);
  const rotateCToN = watch(ROTATE_CLIENT_NODE_CERT_FIELD_NAME);
  const k8sEncryption = watch(K8S_ENCRYPTION_TYPE_FIELD);

  //Disable CN or NN encryption toggle if (a)-roots certs are modified OR (b)-CN/NN rotation is enabled
  const disableEncryptToggle =
    rotateNToN ||
    rotateCToN ||
    (encryptionEnabled && (rootCA !== rootCAInitial || clientRootCA !== clientRootCAInitial));
  //Disable root cert fields if (a)-CN/NN encryption is turned off
  const rotationDisabled =
    encryptionEnabled &&
    ((enableNodeToNodeEncryptInitial && enableNodeToNodeEncryptInitial !== enableNNEncryption) ||
      (enableClientToNodeEncryptInitial &&
        enableClientToNodeEncryptInitial !== enableCNEncryption));

  const k8srotationDisabled = k8sEncryption !== k8sEncryptionTypeInital;

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

  useUpdateEffect(() => {
    if (k8sEncryption === K8sEncryptionOption.EnableBoth) {
      setValue(ENABLE_NODE_NODE_ENCRYPTION_NAME, true);
      setValue(ENABLE_CLIENT_NODE_ENCRYPTION_NAME, true);
      setValue(USE_SAME_CERTS_FIELD_NAME, true);
    }
    if (k8sEncryption === K8sEncryptionOption.NodeToNode) {
      setValue(ENABLE_NODE_NODE_ENCRYPTION_NAME, true);
      setValue(ENABLE_CLIENT_NODE_ENCRYPTION_NAME, false);
      setValue(USE_SAME_CERTS_FIELD_NAME, false);
    }
    if (k8sEncryption === K8sEncryptionOption.ClienToNode) {
      setValue(ENABLE_NODE_NODE_ENCRYPTION_NAME, false);
      setValue(ENABLE_CLIENT_NODE_ENCRYPTION_NAME, true);
      setValue(USE_SAME_CERTS_FIELD_NAME, false);
    }
  }, [k8sEncryption]);

  if (isItKubernetesUniverse)
    return (
      <Box mt={3} className={classes.container} p={2}>
        <YBTooltip
          title={
            disableEncryptToggle
              ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
              : ''
          }
          placement="top"
        >
          <span>
            <YBRadioGroupField
              name="k8sEncryptionType"
              options={K8S_ENCRYPTION_OPTIONS}
              control={control}
              orientation="vertical"
              style={{ gap: 8, marginBottom: '16px' }}
              isDisabled={disableEncryptToggle}
            />
          </span>
        </YBTooltip>

        <CertificateField
          name={
            k8sEncryption === K8sEncryptionOption.ClienToNode
              ? CertTypes.clientRootCA
              : CertTypes.rootCA
          }
          disabled={k8srotationDisabled}
          label={
            encryptionEnabled && rootCA
              ? t('universeActions.encryptionInTransit.rotateCA')
              : t('universeActions.encryptionInTransit.selectCA')
          }
          activeCert={''}
          tooltipMsg={
            k8srotationDisabled
              ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
              : ''
          }
        />
      </Box>
    );
  else
    return (
      <>
        <Box mt={3}>
          <YBCheckboxField
            control={control}
            name={USE_SAME_CERTS_FIELD_NAME}
            label={t('universeActions.encryptionInTransit.useSameCert')}
            labelProps={{ className: classes.eitLabels }}
            inputProps={{
              'data-testid': 'UseSameCert-Checkbox'
            }}
          />
        </Box>
        <Box mt={1} display="flex" flexDirection="column" className={classes.container}>
          <Box className={classes.subContainer}>
            <Box display="flex" alignItems="center" justifyContent="space-between">
              <Box>
                <Typography variant="h6">
                  {rootAndClientRootCASame
                    ? t('universeActions.encryptionInTransit.enableCNAndNNEncryption')
                    : t('universeActions.encryptionInTransit.enableNNEncryption')}
                </Typography>
              </Box>
              <Box>
                <YBTooltip
                  title={
                    disableEncryptToggle
                      ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
                      : ''
                  }
                  placement="top"
                >
                  <span>
                    <YBToggleField
                      name={ENABLE_NODE_NODE_ENCRYPTION_NAME}
                      inputProps={{
                        'data-testid': 'EnableEncryptionInTransit-Toggle'
                      }}
                      control={control}
                      disabled={disableEncryptToggle}
                    />
                  </span>
                </YBTooltip>
              </Box>
            </Box>
            <Box mt={2}>
              <CertificateField
                name={CertTypes.rootCA}
                disabled={!enableNNEncryption || rotationDisabled}
                label={
                  encryptionEnabled && rootCA
                    ? t('universeActions.encryptionInTransit.rotateCA')
                    : t('universeActions.encryptionInTransit.selectCA')
                }
                activeCert={''}
                tooltipMsg={
                  rotationDisabled
                    ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
                    : ''
                }
              />
            </Box>
            {rootAndClientRootCASame && clientRootCA !== rootCA && (
              <Box mt={2}>
                <YBAlert
                  text={t('universeActions.encryptionInTransit.changeCTNCertWarning')}
                  variant={AlertVariant.Warning}
                  open={true}
                />
              </Box>
            )}
          </Box>

          {!rootAndClientRootCASame && (
            <>
              <Divider />
              <Box className={classes.subContainer}>
                <Box display="flex" alignItems="center" justifyContent="space-between">
                  <Box>
                    <Typography variant="h6">
                      {t('universeActions.encryptionInTransit.enableCNEncryption')}
                    </Typography>
                  </Box>
                  <Box>
                    <YBTooltip
                      title={
                        disableEncryptToggle
                          ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
                          : ''
                      }
                      placement="top"
                    >
                      <span>
                        <YBToggleField
                          name={ENABLE_CLIENT_NODE_ENCRYPTION_NAME}
                          inputProps={{
                            'data-testid': 'EnableEncryptionInTransit-Toggle'
                          }}
                          control={control}
                          disabled={disableEncryptToggle}
                        />
                      </span>
                    </YBTooltip>
                  </Box>
                </Box>
                <Box mt={2}>
                  <CertificateField
                    name={CertTypes.clientRootCA}
                    disabled={!enableCNEncryption || rotationDisabled}
                    label={
                      encryptionEnabled && clientRootCA
                        ? t('universeActions.encryptionInTransit.rotateCA')
                        : t('universeActions.encryptionInTransit.selectCA')
                    }
                    activeCert=""
                    tooltipMsg={
                      rotationDisabled
                        ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
                        : ''
                    }
                  />
                </Box>
                <Box
                  mt={2}
                  hidden={
                    !(encryptionEnabled && clientRootCA && clientRootCA !== clientRootCAInitial)
                  }
                >
                  <YBAlert
                    text={t('universeActions.encryptionInTransit.changeCTNCertWarning')}
                    variant={AlertVariant.Warning}
                    open={true}
                  />
                </Box>
              </Box>
            </>
          )}
        </Box>
      </>
    );
};
