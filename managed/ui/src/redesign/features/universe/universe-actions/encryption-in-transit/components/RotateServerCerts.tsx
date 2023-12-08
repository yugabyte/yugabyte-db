import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box, Divider, Typography } from '@material-ui/core';
import { YBCheckboxField, YBTooltip } from '../../../../../components';
import {
  useEITStyles,
  EncryptionInTransitFormValues,
  ROTATE_NODE_NODE_CERT_FIELD_NAME,
  ROTATE_CLIENT_NODE_CERT_FIELD_NAME,
  NODE_NODE_CERT_FIELD_NAME,
  CLIENT_NODE_CERT_FIELD_NAME,
  ENABLE_NODE_NODE_ENCRYPTION_NAME,
  ENABLE_CLIENT_NODE_ENCRYPTION_NAME
} from '../EncryptionInTransitUtils';

interface RotateServerCertsProps {
  initialValues: EncryptionInTransitFormValues;
}

export const RotateServerCerts: FC<RotateServerCertsProps> = ({ initialValues }) => {
  const { t } = useTranslation();
  const classes = useEITStyles();
  const { control, watch } = useFormContext<EncryptionInTransitFormValues>();

  //Initial Form value
  const encryptionEnabled = initialValues.enableUniverseEncryption;
  const rootCAInitial = initialValues.rootCA;
  const clientRootCAInitial = initialValues.clientRootCA;
  const enableNodeToNodeEncryptInitial = initialValues.enableNodeToNodeEncrypt;
  const enableClientToNodeEncryptInitial = initialValues.enableClientToNodeEncrypt;

  //form values
  const rootCA = watch(NODE_NODE_CERT_FIELD_NAME);
  const clientRootCA = watch(CLIENT_NODE_CERT_FIELD_NAME);
  const enableNodeToNodeEncrypt = watch(ENABLE_NODE_NODE_ENCRYPTION_NAME);
  const enableClientToNodeEncrypt = watch(ENABLE_CLIENT_NODE_ENCRYPTION_NAME);

  const rotationDisabled =
    encryptionEnabled &&
    (enableNodeToNodeEncryptInitial !== enableNodeToNodeEncrypt ||
      enableClientToNodeEncryptInitial !== enableClientToNodeEncrypt);

  const rootCAModified = encryptionEnabled && rootCAInitial !== rootCA;
  const clientRootCAModified = encryptionEnabled && clientRootCAInitial !== clientRootCA;

  return (
    <>
      <Box mt={3}>
        <Typography variant="body2" className={classes.subHeading}>
          {t('universeActions.encryptionInTransit.rotateServerCert')}
        </Typography>
      </Box>
      <Box mt={1} display="flex" flexDirection="column" className={classes.container}>
        <Box py={2} px={1} hidden={!enableNodeToNodeEncrypt}>
          <Box display="flex" alignItems="center" justifyContent="space-between">
            <YBTooltip
              title={
                rootCAModified
                  ? t('universeActions.encryptionInTransit.rootCAModifiedWarning')
                  : rotationDisabled
                  ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
                  : ''
              }
              placement="top"
            >
              <span>
                <YBCheckboxField
                  control={control}
                  name={ROTATE_NODE_NODE_CERT_FIELD_NAME}
                  label={t('universeActions.encryptionInTransit.rotateNToNServerCert')}
                  labelProps={{ className: classes.eitLabels }}
                  disabled={rootCAModified || rotationDisabled}
                />
              </span>
            </YBTooltip>
          </Box>
        </Box>

        <Divider />

        <Box py={2} px={1} hidden={!enableClientToNodeEncrypt}>
          <Box display="flex" alignItems="center" justifyContent="space-between">
            <YBTooltip
              title={
                clientRootCAModified
                  ? t('universeActions.encryptionInTransit.clientRootCAModifiedWarning')
                  : rotationDisabled
                  ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
                  : ''
              }
              placement="top"
            >
              <span>
                <YBCheckboxField
                  control={control}
                  name={ROTATE_CLIENT_NODE_CERT_FIELD_NAME}
                  label={t('universeActions.encryptionInTransit.rotateCToNServerCert')}
                  labelProps={{ className: classes.eitLabels }}
                  disabled={clientRootCAModified || rotationDisabled}
                />
              </span>
            </YBTooltip>
          </Box>
        </Box>
      </Box>
    </>
  );
};
