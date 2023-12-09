import { useContext } from 'react';
import _ from 'lodash';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { Box, Typography, useMediaQuery } from '@material-ui/core';
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
import { getPrimaryCluster } from '../../../utils/helpers';
import {
  ClusterModes,
  ClusterType,
  RunTimeConfigEntry,
  UniverseFormConfigurationProps
} from '../../../utils/dto';
import { useSectionStyles } from '../../../universeMainStyle';

export const CloudConfiguration = ({ runtimeConfigs }: UniverseFormConfigurationProps) => {
  const classes = useSectionStyles();
  const { t } = useTranslation();
  const isLargeDevice = useMediaQuery('(min-width:1400px)');

  //feature flagging
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const isGeoPartitionEnabled =
    featureFlags.test.enableGeoPartitioning || featureFlags.released.enableGeoPartitioning;

  // Value of runtime config key
  const enableDedicatedNodesObject = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.ui.enable_dedicated_nodes'
  );
  const useK8CustomResourcesObject = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.use_k8s_custom_resources'
  );
  const useK8CustomResources = !!(useK8CustomResourcesObject?.value === 'true');
  const isDedicatedNodesEnabled = !!(enableDedicatedNodesObject?.value === 'true');

  //form context
  const { clusterType, mode, universeConfigureTemplate } = useContext(UniverseFormContext)[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isEditMode = mode === ClusterModes.EDIT; //Form is in edit mode
  const isEditPrimary = isEditMode && isPrimary; //Editing Primary Cluster

  //For async cluster creation show providers based on primary clusters provider type
  const primaryProviderCode = !isPrimary
    ? _.get(getPrimaryCluster(universeConfigureTemplate), 'userIntent.providerType', null)
    : null;

  return (
    <Box
      className={classes.sectionContainer}
      style={{ flexDirection: isLargeDevice ? 'row' : 'column' }}
      data-testid="CloudConfiguration-Section"
    >
      <Box width="600px" display="flex" flexDirection="column">
        <Box mb={4}>
          <Typography variant="h4">{t('universeForm.cloudConfig.title')}</Typography>
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
          <Box mt={isPrimary ? 2 : 0}>
            <MasterPlacementField
              isPrimary={isPrimary}
              useK8CustomResources={useK8CustomResources}
            />
          </Box>
        )}
        <Box mt={2}>
          <TotalNodesField disabled={false} />
        </Box>
        <Box mt={2}>
          <ReplicationFactor disabled={isEditMode} isPrimary={isPrimary} />
        </Box>
        {isPrimary && isGeoPartitionEnabled && (
          <Box mt={2} display="flex" flexDirection="column">
            <DefaultRegionField disabled={isEditPrimary} />
          </Box>
        )}
      </Box>
      <Box mt={isLargeDevice ? 0 : 4}>
        <PlacementsField
          disabled={false}
          isPrimary={isPrimary}
          isGeoPartitionEnabled={isGeoPartitionEnabled}
          isEditMode={isEditMode}
        />
      </Box>
    </Box>
  );
};
