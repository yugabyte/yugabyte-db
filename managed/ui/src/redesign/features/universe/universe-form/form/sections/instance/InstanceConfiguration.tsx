import React, { FC, useContext } from 'react';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { Box, Grid, Typography } from '@material-ui/core';
import {
  AssignPublicIPField,
  ClientToNodeTLSField,
  EncryptionAtRestField,
  InstanceTypeField,
  KMSConfigField,
  NodeToNodeTLSField,
  RootCertificateField,
  TimeSyncField,
  VolumeInfoField,
  YEDISField,
  YCQLField,
  YSQLField,
  DedicatedNodesField
} from '../../fields';
import { YBLabel } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import {
  AccessKey,
  CloudType,
  ClusterModes,
  ClusterType,
  RunTimeConfigEntry
} from '../../../utils/dto';
import {
  PROVIDER_FIELD,
  EAR_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  ACCESS_KEY_FIELD
} from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';

export const InstanceConfiguration: FC = () => {
  const classes = useSectionStyles();
  const { t } = useTranslation();
  const currentCustomer = useSelector((state: any) => state.customer.currentCustomer);
  const customerUUID = currentCustomer?.data?.uuid;

  //fetch run time configs
  const { data: customerRuntimeConfigs } = useQuery(QUERY_KEY.fetchCustomerRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, customerUUID)
  );
  const authEnforcedObject = customerRuntimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.universe.auth.is_enforced'
  );
  const isAuthEnforced = !!(authEnforcedObject?.value === 'true');

  //Feature flags
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const isDedicatedNodesEnabled =
    featureFlags.test.enableDedicatedNodes || featureFlags.released.enableDedicatedNodes;

  //form context
  const { mode, clusterType, newUniverse } = useContext(UniverseFormContext)[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isCreateMode = mode === ClusterModes.CREATE; //Form is in edit mode
  const isCreatePrimary = isCreateMode && isPrimary; //Creating Primary Cluster
  const isCreateRR = !newUniverse && isCreateMode && !isPrimary; //Adding Async Cluster to an existing Universe

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

  return (
    <Box className={classes.sectionContainer} data-testid="instance-config-section">
      <Typography className={classes.sectionHeaderFont}>
        {t('universeForm.instanceConfig.title')}
      </Typography>
      <Box width="100%" display="flex" flexDirection="column" justifyContent="center">
        <Box mt={4}>
          <Grid container spacing={3}>
            <Grid lg={6} item container>
              <InstanceTypeField />
            </Grid>
          </Grid>
        </Box>

        <Box mt={1}>
          <Grid container spacing={3}>
            <Grid lg={6} item container>
              <Box display="flex" width="100%">
                <YBLabel></YBLabel>
                <Box flex={1}>
                  <VolumeInfoField
                    isEditMode={!isCreateMode}
                    isPrimary={isPrimary}
                    disableIops={!isCreatePrimary && !isCreateRR}
                    disableThroughput={!isCreatePrimary && !isCreateRR}
                    disableStorageType={!isCreatePrimary && !isCreateRR}
                    disableVolumeSize={false}
                    disableNumVolumes={!isCreateMode && provider?.code === CloudType.kubernetes}
                  />
                </Box>
              </Box>
            </Grid>
          </Grid>
        </Box>

        {isCreatePrimary && isDedicatedNodesEnabled && (
          <Box mt={2}>
            <Grid container>
              <Grid lg={6} item container>
                <DedicatedNodesField disabled={false} />
              </Grid>
            </Grid>
          </Box>
        )}

        {[CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code) && (
          <>
            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <AssignPublicIPField disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>

            {currentAccessKeyInfo?.keyInfo?.showSetUpChrony === false && (
              <Box mt={2}>
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
            {(clientNodeTLSEnabled || nodeNodeTLSEnabled) && (
              <Box mt={1}>
                <Grid container spacing={3}>
                  <Grid lg={6} item container>
                    <RootCertificateField
                      disabled={!isCreatePrimary}
                      isPrimary={isPrimary}
                      isCreateMode={isCreateMode}
                    />
                  </Grid>
                </Grid>
              </Box>
            )}

            <Box mt={2}>
              <YSQLField disabled={!isCreatePrimary} enforceAuth={isAuthEnforced} />
            </Box>

            <Box mt={2}>
              <YCQLField disabled={!isCreatePrimary} enforceAuth={isAuthEnforced} />
            </Box>

            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <YEDISField disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>

            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <NodeToNodeTLSField disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>

            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <ClientToNodeTLSField disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>
            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <EncryptionAtRestField disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>

            {encryptionEnabled && isPrimary && (
              <Box mt={1}>
                <Grid container spacing={3}>
                  <Grid lg={6} item container>
                    <KMSConfigField disabled={!isCreatePrimary} />
                  </Grid>
                </Grid>
              </Box>
            )}
          </>
        )}
      </Box>
    </Box>
  );
};
