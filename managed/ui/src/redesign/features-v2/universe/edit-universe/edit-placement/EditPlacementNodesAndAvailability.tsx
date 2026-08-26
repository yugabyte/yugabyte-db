import { useMemo, useRef } from 'react';
import { isEqual } from 'lodash';
import { mui } from '@yugabyte-ui-library/core';
import { useToggle } from 'react-use';
import {
  buildPlacementEditComparePayload,
  getNodesAvailabilityDefaultsForEditPlacement,
  getResilienceAndRegionsProps,
  useGetEditPlacementContext
} from './EditPlacementUtils';
import { EditPlacementContextProps, EditPlacementSteps } from './EditPlacementContext';
import GeoPartitionBreadCrumb from '../../geo-partition/add/GeoPartitionBreadCrumbs';
import { NodesAvailability } from '../../create-universe/steps';
import { UniverseActionButtons } from '../../create-universe/components/UniverseActionButtons';
import { useTranslation } from 'react-i18next';
import { EditPlacementConfirmModal } from './EditPlacementConfirmModal';
import {
  CreateUniverseContext,
  createUniverseFormProps,
  StepsRef
} from '../../create-universe/CreateUniverseContext';
import { normalizeEditPlacementNodesAvailability } from './normalizeEditPlacementNodesAvailability';
import { isKubernetesUniverse, useEditUniverseContext } from '../EditUniverseUtils';
import { CloudType } from '@app/redesign/helpers/dtos';
import { useYBToast } from '../../create-universe/helpers/ToastUtils';

const { Box } = mui;

export const EditPlacementNodesAndAvailability = () => {
  const nodesAndAvailabilityRef = useRef<StepsRef>(null);
  const [addEditPlacementData, addEditPlacementMethods, extraMethods] =
    useGetEditPlacementContext();
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });
  const { t: tp } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const { universeData, providerRegions } = useEditUniverseContext();
  const isK8s = isKubernetesUniverse(universeData!);
  const [showEditPlacementModal, setShowEditPlacementModal] = useToggle(false);
  const { setNodesAndAvailability, setResilience, setActiveStep } = addEditPlacementMethods;
  const toast = useYBToast();

  const { hideModal, onSubmit, isSubmittingPlacementUpdate, selectedPartitionUUID } = extraMethods;

  const initialPayloadRef = useRef(
    buildPlacementEditComparePayload(
      universeData!,
      getResilienceAndRegionsProps(universeData!, providerRegions, selectedPartitionUUID),
      getNodesAvailabilityDefaultsForEditPlacement(universeData!, selectedPartitionUUID),
      selectedPartitionUUID
    )
  );

  const universeNodesDefaults = useMemo(
    () => getNodesAvailabilityDefaultsForEditPlacement(universeData!, selectedPartitionUUID),
    [universeData, selectedPartitionUUID]
  );

  const baselineRegionCodes = useMemo(
    () =>
      getResilienceAndRegionsProps(universeData!, providerRegions, selectedPartitionUUID).regions.map(
        (r) => r.code
      ),
    [universeData, providerRegions, selectedPartitionUUID]
  );

  const baselineZoneUuidsByRegion = useMemo(() => {
    const map: Record<string, string[]> = {};
    for (const [regionCode, zones] of Object.entries(universeNodesDefaults.availabilityZones ?? {})) {
      map[regionCode] = (zones ?? []).map((z) => z.uuid).filter(Boolean);
    }
    return map;
  }, [universeNodesDefaults]);

  const calculateNodesandAvailability = useMemo(
    () => normalizeEditPlacementNodesAvailability(addEditPlacementData),
    [addEditPlacementData]
  );

  return (
    <CreateUniverseContext.Provider
      value={
        [
          {
            activeStep: 1,
            resilienceAndRegionsSettings: addEditPlacementData.resilience,
            nodesAvailabilitySettings: calculateNodesandAvailability,
            generalSettings: isK8s
              ? {
                  cloud: CloudType.kubernetes,
                  providerConfiguration: { code: CloudType.kubernetes }
                }
              : undefined
          },
          {
            saveNodesAvailabilitySettings: (
              data: EditPlacementContextProps['nodesAndAvailability']
            ) => {
              data && setNodesAndAvailability(data);
            },
            moveToNextPage: () => {
              const { resilience, nodesAndAvailability } = addEditPlacementData;
              if (
                resilience &&
                nodesAndAvailability &&
                isEqual(
                  buildPlacementEditComparePayload(
                    universeData!,
                    resilience,
                    nodesAndAvailability,
                    selectedPartitionUUID
                  ),
                  initialPayloadRef.current
                )
              ) {
                toast.warn(tp('noPlacementChanges'));
                return;
              }
              setShowEditPlacementModal(true);
            },
            saveResilienceAndRegionsSettings: (data: EditPlacementContextProps['resilience']) => {
              data && setResilience(data);
            }
          }
        ] as unknown as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', flexDirection: 'column' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{t('placement')}</>}
          subTitle={<>{t(isK8s ? 'podsAndAvailabilityZone' : 'nodesAndAvailabilityZone')}</>}
        />
        <Box sx={{ display: 'flex', gap: '24px', flexDirection: 'column', mb: 3 }}>
          <NodesAvailability
            ref={nodesAndAvailabilityRef}
            isGeoPartition
            hideDedicatedNodes
            baselineRegionCodes={baselineRegionCodes}
            baselineZoneUuidsByRegion={baselineZoneUuidsByRegion}
          />
        </Box>
        <UniverseActionButtons
          prevButton={{
            text: t('back', { keyPrefix: 'common' }),
            onClick: () => {
              setActiveStep(EditPlacementSteps.RESILIENCE_AND_REGIONS);
            }
          }}
          cancelButton={{
            text: t('cancel', { keyPrefix: 'common' }),
            onClick: () => {
              hideModal();
            }
          }}
          nextButton={{
            text: t('reviewChanges', { keyPrefix: 'editUniverse.placement' }),
            onClick: () => {
              nodesAndAvailabilityRef.current?.onNext();
            }
          }}
        />
        <EditPlacementConfirmModal
          visible={showEditPlacementModal}
          isSubmitting={isSubmittingPlacementUpdate}
          onHide={() => {
            setShowEditPlacementModal(false);
          }}
          onSubmit={() => {
            onSubmit(addEditPlacementData, () => {
              setShowEditPlacementModal(false);
            });
          }}
        />
      </Box>
    </CreateUniverseContext.Provider>
  );
};
