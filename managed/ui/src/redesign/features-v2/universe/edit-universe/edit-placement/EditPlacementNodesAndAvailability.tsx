import { useRef } from 'react';
import { mui } from '@yugabyte-ui-library/core';
import { useToggle } from 'react-use';
import { useGetEditPlacementContext } from './EditPlacementUtils';
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

const { Box } = mui;

export const EditPlacementNodesAndAvailability = () => {
  const nodesAndAvailabilityRef = useRef<StepsRef>(null);
  const [addEditPlacementData, addEditPlacementMethods] = useGetEditPlacementContext();
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });
  const [showEditPlacementModal, setShowEditPlacementModal] = useToggle(false);
  const { setNodesAndAvailability } = addEditPlacementMethods;

  const context = useGetEditPlacementContext();

  const { setActiveStep } = context[1];
  const { hideModal, onSubmit, isSubmittingPlacementUpdate } = context[2];

  return (
    <CreateUniverseContext.Provider
      value={
        ([
          {
            activeStep: 1,
            resilienceAndRegionsSettings: addEditPlacementData.resilience,
            nodesAvailabilitySettings: addEditPlacementData.nodesAndAvailability
          },
          {
            saveNodesAvailabilitySettings: (
              data: EditPlacementContextProps['nodesAndAvailability']
            ) => {
              data && setNodesAndAvailability(data);
            },
            moveToNextPage: () => {
              setShowEditPlacementModal(true);
            }
          }
        ] as unknown) as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', gap: '24px', flexDirection: 'column' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{t('placement')}</>}
          subTitle={<>{t('nodesAndAvailabilityZone')}</>}
        />
        <NodesAvailability
          ref={nodesAndAvailabilityRef}
          isGeoPartition
        />
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
