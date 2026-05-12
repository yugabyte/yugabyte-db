import { useContext, useRef } from 'react';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  AddGeoPartitionContextProps,
  GeoPartition
} from '../AddGeoPartitionContext';
import {
  CreateUniverseContext,
  createUniverseFormProps,
  StepsRef
} from '../../../create-universe/CreateUniverseContext';
import { ResilienceAndRegions } from '../../../create-universe/steps';
import { UniverseActionButtons } from '../../../create-universe/components/UniverseActionButtons';
import {
  navigateToUniverseSettingsFromWizard,
  useGeoPartitionNavigation
} from '../AddGeoPartitionUtils';
import GeoPartitionBreadCrumb from '../GeoPartitionBreadCrumbs';
import { mui } from '@yugabyte-ui-library/core';
import { NodeAvailabilityProps } from '../../../create-universe/steps/nodes-availability/dtos';

const { Box } = mui;

export const GeoPartitionResilience = () => {
  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;
  const resilienceRef = useRef<StepsRef>(null);
  const { moveToNextPage: goToNextGeoPartitionStep, moveToPreviousPage } =
    useGeoPartitionNavigation();
  const resilienceContextAfterSaveRef = useRef<AddGeoPartitionContextProps | null>(null);
  const { geoPartitions, activeGeoPartitionIndex, universeData } = addGeoPartitionContext;
  const { updateGeoPartition } = addGeoPartitionMethods;
  return (
    <CreateUniverseContext.Provider
      value={
        ([
          {
            activeStep: 1,
            resilienceAndRegionsSettings: geoPartitions[activeGeoPartitionIndex].resilience,
            generalSettings: {
              providerConfiguration: {
                uuid: universeData?.spec?.clusters[0].provider_spec?.provider ?? ''
              }
            }
          },
          {
            setResilienceType: () => {},
            saveResilienceAndRegionsSettings: (data: GeoPartition['resilience']) => {
              updateGeoPartition({
                geoPartition: {
                  ...geoPartitions[activeGeoPartitionIndex],
                  resilience: data
                },
                activeGeoPartitionIndex: activeGeoPartitionIndex
              });
              // Snapshot for goToNextGeoPartitionStep after a deliberate Next; avoid navigating when
              // ResilienceAndRegions saves from effects (e.g. guided → expert mode).
              resilienceContextAfterSaveRef.current = {
                ...addGeoPartitionContext,
                geoPartitions: geoPartitions.map((gp, idx) =>
                  idx === activeGeoPartitionIndex
                    ? {
                        ...gp,
                        resilience: data
                      }
                    : gp
                )
              };
            },
            saveNodesAvailabilitySettings: (data: NodeAvailabilityProps) => {
              updateGeoPartition({
                geoPartition: {
                  ...geoPartitions[activeGeoPartitionIndex],
                  nodesAndAvailability: data
                },
                activeGeoPartitionIndex: activeGeoPartitionIndex
              });
            },
            moveToNextPage: () => {
              const ctx = resilienceContextAfterSaveRef.current;
              if (ctx) {
                goToNextGeoPartitionStep(ctx);
              }
            },
            moveToPreviousPage: () => moveToPreviousPage()
          }
        ] as unknown) as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{geoPartitions[activeGeoPartitionIndex].name}</>}
          subTitle={<>Regions</>}
        />

        <ResilienceAndRegions isGeoPartition hideHelpText ref={resilienceRef} />
        <UniverseActionButtons
          cancelButton={{
            text: 'Cancel',
            onClick: () => navigateToUniverseSettingsFromWizard(universeData)
          }}
          nextButton={{
            text: 'Next',
            onClick: () => {
              resilienceRef.current?.onNext(); //save the data
            }
          }}
          prevButton={{
            onClick: () => moveToPreviousPage()
          }}
        />
      </Box>
    </CreateUniverseContext.Provider>
  );
};
