import React, { FC, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { Box, Grid, Typography } from '@material-ui/core';
import {
  AccessKeysField,
  ARNField,
  DBVersionField,
  DeploymentPortsField,
  IPV6Field,
  NetworkAccessField,
  SystemDField
} from '../../fields';
import { CloudType, ClusterModes, ClusterType } from '../../../utils/dto';
import { PROVIDER_FIELD } from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';
import { UniverseFormContext } from '../../../UniverseFormContainer';

export const AdvancedConfiguration: FC = () => {
  const classes = useSectionStyles();
  const { t } = useTranslation();

  //form context
  const { clusterType, mode, newUniverse }: any = useContext(UniverseFormContext)[0];

  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isCreateMode = mode === ClusterModes.CREATE; //Form is in edit mode
  const isCreatePrimary = isCreateMode && isPrimary; //Editing Primary Cluster
  const isCreateRR = !newUniverse && isCreateMode && !isPrimary; //Adding Async Cluster to an existing Universe

  //field data
  const provider = useWatch({ name: PROVIDER_FIELD });

  if (!provider?.code) return null;

  return (
    <Box className={classes.sectionContainer} data-testid="advanced-config-section">
      <Typography className={classes.sectionHeaderFont}>
        {t('universeForm.advancedConfig.title')}
      </Typography>
      <Box width="100%" display="flex" flexDirection="column" justifyContent="center">
        <Box mt={2}>
          <Grid container spacing={3}>
            <Grid lg={6} item container>
              <DBVersionField disabled={!isCreatePrimary} />
            </Grid>
          </Grid>
        </Box>

        {provider.code !== CloudType.kubernetes && (
          <Box mt={2}>
            <Grid container spacing={3}>
              <Grid lg={6} item container>
                <AccessKeysField disabled={!isCreatePrimary && !isCreateRR} />
              </Grid>
            </Grid>
          </Box>
        )}

        {provider.code === CloudType.aws && (
          <Box mt={2}>
            <Grid container spacing={3}>
              <Grid lg={6} item container>
                <ARNField disabled={!isCreatePrimary && !isCreateRR} />
              </Grid>
            </Grid>
          </Box>
        )}

        {provider.code === CloudType.kubernetes && (
          <>
            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <IPV6Field disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>

            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <NetworkAccessField disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>
          </>
        )}

        {provider.code !== CloudType.kubernetes && (
          <>
            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <SystemDField disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>
            <Box mt={2}>
              <Grid container>
                <Grid lg={6} item container>
                  <DeploymentPortsField disabled={!isCreatePrimary} />
                </Grid>
              </Grid>
            </Box>
          </>
        )}
      </Box>
    </Box>
  );
};
