import React, { FC, useContext } from 'react';
import { useQuery } from 'react-query';
import _ from 'lodash';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { Box, Grid, makeStyles, Typography } from '@material-ui/core';
import {
  DefaultRegionField,
  MasterPlacementField,
  PlacementsField,
  ProvidersField,
  RegionsField,
  ReplicationFactor,
  TotalNodesField,
  UniverseNameField
} from '../../fields';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import { api, QUERY_KEY } from '../../../utils/api';
import { getPrimaryCluster } from '../../../utils/helpers';
import { ClusterModes, ClusterType, RunTimeConfigEntry } from '../../../utils/dto';
import { useSectionStyles } from '../../../universeMainStyle';

const useStyles = makeStyles((theme) => ({
  placementFieldContainer: {
    width: theme.spacing(70)
  }
}));

export const CloudConfiguration: FC = () => {
  const classes = useSectionStyles();
  const helperClasses = useStyles();
  const { t } = useTranslation();
  const currentCustomer = useSelector((state: any) => state.customer.currentCustomer);
  const customerUUID = currentCustomer?.data?.uuid;

  //feature flagging
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const isGeoPartitionEnabled =
    featureFlags.test.enableGeoPartitioning || featureFlags.released.enableGeoPartitioning;

  //fetch run time configs
  const { data: runtimeConfigs } = useQuery(QUERY_KEY.fetchCustomerRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, customerUUID)
  );

  const enableDedicatedNodesObject = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.ui.enable_dedicated_nodes'
  );
  const isDedicatedNodesEnabled = !!(enableDedicatedNodesObject?.value === 'true');

  //form context
  const { clusterType, mode, universeConfigureTemplate } = useContext(UniverseFormContext)[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isEditMode = mode === ClusterModes.EDIT; //Form is in edit mode
  const isCreatePrimary = !isEditMode && isPrimary; //Creating Primary Cluster
  const isEditPrimary = isEditMode && isPrimary; //Editing Primary Cluster

  //For async cluster creation show providers based on primary clusters provider type
  const primaryProviderCode = !isPrimary
    ? _.get(getPrimaryCluster(universeConfigureTemplate), 'userIntent.providerType', null)
    : null;

  return (
    <Box className={classes.sectionContainer} data-testid="CloudConfiguration-Container">
      <Box display="flex" flexDirection="row">
        <Box>
          <Grid container spacing={3}>
            <Grid item lg={12}>
              <Box mb={4}>
                <Typography className={classes.sectionHeaderFont}>
                  {t('universeForm.cloudConfig.title')}
                </Typography>
              </Box>
              {isPrimary && (
                <Box mt={2}>
                  <UniverseNameField disabled={isEditPrimary} />
                </Box>
              )}
              <Box mt={2}>
                <ProvidersField
                  disabled={isEditMode || !isPrimary}
                  filterByProvider={primaryProviderCode}
                />
              </Box>
              <Box mt={2}>
                <RegionsField disabled={false} />
              </Box>
              {isDedicatedNodesEnabled && (
                <Box mt={2}>
                  <MasterPlacementField isPrimary={isPrimary} />
                </Box>
              )}
              <Box mt={2}>
                <TotalNodesField disabled={false} />
              </Box>
              <Box mt={2}>
                <ReplicationFactor disabled={isEditMode} isPrimary={isPrimary} />
              </Box>
              {isCreatePrimary && isGeoPartitionEnabled && (
                <Box mt={2} display="flex" flexDirection="column">
                  <DefaultRegionField />
                </Box>
              )}
            </Grid>
          </Grid>
        </Box>
        <Box ml={5} className={helperClasses.placementFieldContainer}>
          <Grid container spacing={3}>
            <Grid item lg={12}>
              <PlacementsField disabled={false} isPrimary={isPrimary} />
            </Grid>
          </Grid>
        </Box>
      </Box>
    </Box>
  );
};
