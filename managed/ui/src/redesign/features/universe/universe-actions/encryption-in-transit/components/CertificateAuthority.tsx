import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box, Divider, Typography } from '@material-ui/core';
import {
  AlertVariant,
  YBAlert,
  YBCheckboxField,
  YBToggleField,
  YBTooltip
} from '../../../../../components';
import { CertificateField } from '../components/CertificateField';
import {
  ENABLE_NODE_NODE_ENCRYPTION_NAME,
  NODE_NODE_CERT_FIELD_NAME,
  CLIENT_NODE_CERT_FIELD_NAME,
  ENABLE_CLIENT_NODE_ENCRYPTION_NAME,
  ROTATE_NODE_NODE_CERT_FIELD_NAME,
  ROTATE_CLIENT_NODE_CERT_FIELD_NAME,
  useEITStyles,
  CertTypes,
  EncryptionInTransitFormValues,
  USE_SAME_CERTS_FIELD_NAME
} from '../EncryptionInTransitUtils';

interface CertificateAuthorityProps {
  initialValues: EncryptionInTransitFormValues;
}

export const CertificateAuthority: FC<CertificateAuthorityProps> = ({ initialValues }) => {
  const { t } = useTranslation();
  const classes = useEITStyles();
  const { control, watch } = useFormContext<EncryptionInTransitFormValues>();

  //Initial Form value
  const encryptionEnabled = initialValues.enableUniverseEncryption;
  const clientRootCAInitial = initialValues.clientRootCA;
  const rootCAInitial = initialValues.rootCA;
  const enableNodeToNodeEncryptInitial = initialValues.enableNodeToNodeEncrypt;
  const enableClientToNodeEncryptInitial = initialValues.enableClientToNodeEncrypt;

  //Current form value
  const enableNNEncryption = watch(ENABLE_NODE_NODE_ENCRYPTION_NAME);
  const rootCA = watch(NODE_NODE_CERT_FIELD_NAME);
  const enableCNEncryption = watch(ENABLE_CLIENT_NODE_ENCRYPTION_NAME);
  const clientRootCA = watch(CLIENT_NODE_CERT_FIELD_NAME);
  const rootAndClientRootCASame = watch(USE_SAME_CERTS_FIELD_NAME);
  const rotateNToN = watch(ROTATE_NODE_NODE_CERT_FIELD_NAME);
  const rotateCToN = watch(ROTATE_CLIENT_NODE_CERT_FIELD_NAME);

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
