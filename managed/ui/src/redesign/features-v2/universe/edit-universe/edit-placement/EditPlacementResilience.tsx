import { useRef } from 'react';
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
import { useEditUniverseContext } from '../EditUniverseUtils';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import { getResilienceAndRegionsProps, useGetEditPlacementContext } from './EditPlacementUtils';
import { EditPlacementSteps } from './EditPlacementContext';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';

const { Box } = mui;

export const EditPlacementResilience = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const [, , { selectedPartitionUUID }] = useGetEditPlacementContext();
  const resilienceProps = getResilienceAndRegionsProps(
    universeData!,
    providerRegions,
    selectedPartitionUUID
  );
  const resilienceRef = useRef<StepsRef>(null);
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });
  const [{ resilience }, addEditPlacementMethods, { hideModal }] = useGetEditPlacementContext();

  return (
    <CreateUniverseContext.Provider
      value={
        ([
          {
            activeStep: 1,
            resilienceAndRegionsSettings: resilience ?? resilienceProps,
            generalSettings: {
              providerConfiguration: {
                uuid: universeData?.spec?.clusters[0].provider_spec?.provider ?? ''
              }
            }
          },
          {
            setResilienceType: () => {},
            saveResilienceAndRegionsSettings: (data: ResilienceAndRegionsProps) => {
              addEditPlacementMethods.setResilience(data);
              addEditPlacementMethods.setActiveStep(
                EditPlacementSteps.NODES_AND_AVAILABILITY_ZONES
              );
            },
            saveNodesAvailabilitySettings: (data: NodeAvailabilityProps) => {
              addEditPlacementMethods.setNodesAndAvailability(data);
            },
            moveToNextPage: () => {},
            moveToPreviousPage: () => {}
          }
        ] as unknown) as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '10px' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{t('placement')}</>}
          subTitle={<>{t('resilienceAndRegions')}</>}
        />
        <ResilienceAndRegions isGeoPartition hideHelpText ref={resilienceRef} />
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
