import React, { useContext } from 'react';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { Box, Grid, Typography, makeStyles } from '@material-ui/core';
import {
  AssignPublicIPField,
  ClientToNodeTLSField,
  EncryptionAtRestField,
  KMSConfigField,
  NodeToNodeTLSField,
  RootCertificateField,
  TimeSyncField,
  YEDISField,
  YCQLField,
  YSQLField
} from '../../fields';
import { YBLabel } from '../../../../../../components';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import {
  AccessKey,
  CloudType,
  ClusterModes,
  ClusterType,
  RunTimeConfigEntry,
  UniverseFormConfigurationProps
} from '../../../utils/dto';
import {
  PROVIDER_FIELD,
  EAR_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  ACCESS_KEY_FIELD
} from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';

const useStyles = makeStyles((theme) => ({
  settingsContainer: {
    display: 'flex',
    flexDirection: 'column',
    marginTop: theme.spacing(5),
    width: '708px'
  },
  settingsContainerBorder: {
    border: '1px solid #E5E5E6',
    backgroundColor: theme.palette.common.white,
    borderRadius: theme.spacing(1),
    marginTop: theme.spacing(2)
  },
  settingsContainerDivider: {
    height: theme.spacing(0),
    border: '0.5px solid #E5E5E6',
    marginTop: theme.spacing(2)
  }
}));

export const SecurityConfiguration = ({ runtimeConfigs }: UniverseFormConfigurationProps) => {
  const classes = useSectionStyles();
  const helperClasses = useStyles();
  const { t } = useTranslation();

  // Value of runtime config key
  const authEnforcedObject = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.universe.auth.is_enforced'
  );
  const isAuthEnforced = !!(authEnforcedObject?.value === 'true');

  //form context
  const { mode, clusterType } = useContext(UniverseFormContext)[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isCreateMode = mode === ClusterModes.CREATE; //Form is in edit mode
  const isCreatePrimary = isCreateMode && isPrimary; //Creating Primary Cluster

  //field data
  const provider = useWatch({ name: PROVIDER_FIELD });
  const encryptionEnabled = useWatch({ name: EAR_FIELD });
  const clientNodeTLSEnabled = useWatch({ name: CLIENT_TO_NODE_ENCRYPT_FIELD });
  const nodeNodeTLSEnabled = useWatch({ name: NODE_TO_NODE_ENCRYPT_FIELD });
  const accessKey = useWatch({ name: ACCESS_KEY_FIELD });

  //access key info
  const accessKeys = useSelector((state: any) => state.cloud.accessKeys);
  const currentAccessKeyInfo = accessKeys.data.find(
    (key: AccessKey) => key.idKey.providerUUID === provider?.uuid && key.idKey.keyCode === accessKey
  );

  if (!provider?.code) return null;

  return (
    <Box className={classes.sectionContainer} data-testid="security-config-section">
      <Typography className={classes.sectionHeaderFont}>
        {t('universeForm.securityConfig.title')}
      </Typography>
      <Box width="100%" display="flex" flexDirection="column" justifyContent="center">
        {[CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code) && (
          <>
            <Box data-testid="IPSettings-Container" className={helperClasses.settingsContainer}>
              <Typography className={classes.subsectionHeaderFont}>
                {t('universeForm.securityConfig.IPSettings.title')}
              </Typography>

              <Box className={helperClasses.settingsContainerBorder}>
                <Grid container>
                  <Grid lg={6} item container>
                    <AssignPublicIPField disabled={!isCreatePrimary} />
                  </Grid>
                </Grid>
              </Box>
            </Box>

            {currentAccessKeyInfo?.keyInfo?.showSetUpChrony === false && (
              <Box mt={2} ml={2}>
                <Grid container>
                  <Grid lg={6} item container>
                    <TimeSyncField disabled={!isCreateMode} />
                  </Grid>
                </Grid>
              </Box>
            )}
          </>
        )}

        {[
          CloudType.aws,
          CloudType.gcp,
          CloudType.azu,
          CloudType.onprem,
          CloudType.kubernetes
        ].includes(provider?.code) && (
          <>
            <Box
              data-testid="AuthenticationSettings-Container"
              className={helperClasses.settingsContainer}
            >
              <Typography className={classes.subsectionHeaderFont}>
                {t('universeForm.securityConfig.authSettings.title')}
              </Typography>

              <Box className={helperClasses.settingsContainerBorder}>
                <Box mt={3} ml={2}>
                  <YSQLField disabled={!isCreatePrimary} enforceAuth={isAuthEnforced} />
                </Box>
                <Box className={helperClasses.settingsContainerDivider}></Box>

                <Box mt={3} ml={2}>
                  <YCQLField disabled={!isCreatePrimary} enforceAuth={isAuthEnforced} />
                </Box>
                <Box className={helperClasses.settingsContainerDivider}></Box>

                <Box mt={3} ml={2} mb={2}>
                  <Grid container>
                    <Grid lg={6} item container>
                      <YEDISField disabled={!isCreatePrimary} />
                    </Grid>
                  </Grid>
                </Box>
              </Box>
            </Box>

            <Box
              data-testid="EncryptionSettings-Container"
              className={helperClasses.settingsContainer}
            >
              <Typography className={classes.subsectionHeaderFont}>
                {t('universeForm.securityConfig.encryptionSettings.title')}
              </Typography>

              <Box className={helperClasses.settingsContainerBorder}>
                <Box mt={3} ml={2}>
                  <YBLabel dataTestId="YEDISField-Label">
                    {t('universeForm.securityConfig.encryptionSettings.encryptionInTransit')}
                  </YBLabel>
                  <Grid container>
                    <Grid lg={6} item container>
                      <NodeToNodeTLSField disabled={!isCreatePrimary} />
                    </Grid>
                  </Grid>
                </Box>

                <Box mt={3} ml={2}>
                  <Grid container>
                    <Grid lg={6} item container>
                      <ClientToNodeTLSField disabled={!isCreatePrimary} />
                    </Grid>
                  </Grid>
                </Box>

                {(clientNodeTLSEnabled || nodeNodeTLSEnabled) && (
                  <Box mt={3} ml={2}>
                    <RootCertificateField
                      disabled={!isCreatePrimary}
                      isPrimary={isPrimary}
                      isCreateMode={isCreateMode}
                    />
                  </Box>
                )}
                <Box className={helperClasses.settingsContainerDivider}></Box>

                <Box mt={3} ml={2} mb={2}>
                  <YBLabel dataTestId="YEDISField-Label">
                    {t('universeForm.securityConfig.encryptionSettings.encryptionAtRest')}
                  </YBLabel>
                  <Grid container>
                    <Grid lg={6} item container>
                      <EncryptionAtRestField disabled={!isCreatePrimary} />
                    </Grid>
                  </Grid>
                </Box>

                {encryptionEnabled && isPrimary && (
                  <Box mt={2} ml={2} mb={2}>
                    <Grid container spacing={3}>
                      <Grid lg={10} item container>
                        <KMSConfigField disabled={!isCreatePrimary} />
                      </Grid>
                    </Grid>
                  </Box>
                )}
              </Box>
            </Box>
          </>
        )}
      </Box>
    </Box>
  );
};
