import { useContext } from 'react';
import _ from 'lodash';
import { useWatch } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, Typography, useMediaQuery } from '@material-ui/core';
import { useQuery } from 'react-query';

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
import { RuntimeConfigKey } from '../../../../../../helpers/constants';
import { PROVIDER_FIELD } from '../../../utils/constants';
import { api, runtimeConfigQueryKey } from '../../../../../../helpers/api';

import {
  ClusterModes,
  ClusterType,
  RunTimeConfigEntry,
  UniverseFormConfigurationProps
} from '../../../utils/dto';
import { YBProvider } from '../../../../../../../components/configRedesign/providerRedesign/types';

import { useSectionStyles } from '../../../universeMainStyle';

export const CloudConfiguration = ({ runtimeConfigs }: UniverseFormConfigurationProps) => {
  const classes = useSectionStyles();
  const { t } = useTranslation();
  const isLargeDevice = useMediaQuery('(min-width:1400px)');

  const provider: YBProvider = useWatch({ name: PROVIDER_FIELD });

  const providerRuntimeConfigQuery = useQuery(
    runtimeConfigQueryKey.providerScope(provider?.uuid),
    () => api.fetchRuntimeConfigs(provider?.uuid, true),
    { enabled: !!provider?.uuid }
  );

  const isGeoPartitionEnabled =
    providerRuntimeConfigQuery.data?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.GEO_PARTITIONING_UI_FEATURE_FLAG
    )?.value === 'true';

  // Value of runtime config key
  const enableDedicatedNodesObject = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.ENABLE_DEDICATED_NODES
  );
  const useK8CustomResourcesObject = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.USE_K8_CUSTOM_RESOURCES_FEATURE_FLAG
  );
  const useK8CustomResources = !!(useK8CustomResourcesObject?.value === 'true');
  const isDedicatedNodesEnabled = !!(enableDedicatedNodesObject?.value === 'true');

  //form context
  const { clusterType, mode, universeConfigureTemplate, isViewMode } = useContext(
    UniverseFormContext
  )[0];

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
          <RegionsField disabled={isViewMode} />
        </Box>
        {isDedicatedNodesEnabled && (
          <Box mt={isPrimary ? 2 : 0}>
            <MasterPlacementField
              isPrimary={isPrimary}
              useK8CustomResources={useK8CustomResources}
              disabled={isViewMode}
              isEditMode={isEditMode}
            />
          </Box>
        )}
        <Box mt={2}>
          <TotalNodesField disabled={isViewMode} />
        </Box>
        <Box mt={2}>
          <ReplicationFactor
            disabled={isViewMode}
            isPrimary={isPrimary}
            isEditMode={isEditMode}
          />
        </Box>
        {isPrimary && isGeoPartitionEnabled && (
          <Box mt={2} display="flex" flexDirection="column">
            <DefaultRegionField disabled={isEditPrimary || isViewMode} />
          </Box>
        )}
      </Box>
      <Box mt={isLargeDevice ? 0 : 4}>
        <PlacementsField
          disabled={isViewMode}
          isPrimary={isPrimary}
          isGeoPartitionEnabled={isGeoPartitionEnabled}
          isEditMode={isEditMode}
        />
      </Box>
    </Box>
  );
};
