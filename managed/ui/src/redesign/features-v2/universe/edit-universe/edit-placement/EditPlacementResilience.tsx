import { useMemo, useRef } from 'react';
import { mui } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import {
  CreateUniverseContext,
  createUniverseFormProps,
  StepsRef
} from '../../create-universe/CreateUniverseContext';
import GeoPartitionBreadCrumb from '../../geo-partition/add/GeoPartitionBreadCrumbs';
import { ResilienceAndRegions } from '../../create-universe/steps';
import { UniverseActionButtons } from '../../create-universe/components/UniverseActionButtons';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getExistingGeoPartitions } from '../../geo-partition/add/AddGeoPartitionUtils';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import {
  getNodesAvailabilityDefaultsForEditPlacement,
  getResilienceAndRegionsProps,
  isCurrentConfigSupportedByGuidedMode,
  resolveEditPlacementNodesOnSave,
  useGetEditPlacementContext
} from './EditPlacementUtils';
import { EditPlacementSteps } from './EditPlacementContext';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';

const { Box } = mui;

export const EditPlacementResilience = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const isGeoPartitionUniverse = getExistingGeoPartitions(universeData!).length > 0;
  const [, , { selectedPartitionUUID }] = useGetEditPlacementContext();
  const resilienceProps = getResilienceAndRegionsProps(
    universeData!,
    providerRegions,
    selectedPartitionUUID
  );
  const resilienceRef = useRef<StepsRef>(null);
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });
  const [{ resilience, nodesAndAvailability }, addEditPlacementMethods, { hideModal }] =
    useGetEditPlacementContext();

  const universeNodesDefaults = useMemo(
    () => getNodesAvailabilityDefaultsForEditPlacement(universeData!, selectedPartitionUUID),
    [universeData, selectedPartitionUUID]
  );

  const enableGuidedMode = useMemo(
    () => isCurrentConfigSupportedByGuidedMode(resilienceProps, universeNodesDefaults).isSupported,
    [resilienceProps, universeNodesDefaults]
  );

  return (
    <CreateUniverseContext.Provider
      value={
        [
          {
            activeStep: 1,
            resilienceAndRegionsSettings: resilience ?? resilienceProps,
            nodesAvailabilitySettings: nodesAndAvailability,
            generalSettings: {
              cloud: primaryCluster?.placement_spec?.cloud_list?.[0]?.code,
              providerConfiguration: {
                uuid: primaryCluster?.provider_spec?.provider ?? '',
                code: primaryCluster?.placement_spec?.cloud_list?.[0]?.code
              }
            }
          },
          {
            setResilienceType: () => {},
            saveResilienceAndRegionsSettings: (data: ResilienceAndRegionsProps) => {
              addEditPlacementMethods.setResilience(data);
              // Seed universe placement once; do not overwrite on every persist/unmount flush
              // (region-change clears to {} so nodes step can rebuild for newly selected regions).
              if (!nodesAndAvailability) {
                addEditPlacementMethods.setNodesAndAvailability(universeNodesDefaults);
              }
            },
            saveNodesAvailabilitySettings: (data: NodeAvailabilityProps) => {
              addEditPlacementMethods.setNodesAndAvailability(
                resolveEditPlacementNodesOnSave(data, universeNodesDefaults)
              );
            },
            moveToNextPage: () => {
              addEditPlacementMethods.setActiveStep(
                EditPlacementSteps.NODES_AND_AVAILABILITY_ZONES
              );
            },
            moveToPreviousPage: () => {}
          }
        ] as unknown as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', flexDirection: 'column' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{t('placement')}</>}
          subTitle={<>{t('resilienceAndRegions')}</>}
        />
        <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px', mb: 3 }}>
          <ResilienceAndRegions
            isGeoPartition={isGeoPartitionUniverse}
            hideHelpText
            ref={resilienceRef}
            disableGuidedMode={!enableGuidedMode}
          />
        </Box>
        <UniverseActionButtons
          cancelButton={{
            text: t('cancel', { keyPrefix: 'common' }),
            onClick: () => {
              hideModal();
            }
          }}
          nextButton={{
            text: t('next', { keyPrefix: 'common' }),
            onClick: () => {
              resilienceRef.current?.onNext(); //save the data
            }
          }}
        />
      </Box>
    </CreateUniverseContext.Provider>
  );
};
