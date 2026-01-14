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
import { useGeoPartitionNavigation } from '../AddGeoPartitionUtils';
import GeoPartitionBreadCrumb from '../GeoPartitionBreadCrumbs';
import { mui } from '@yugabyte-ui-library/core';

const { Box } = mui;

export const GeoPartitionResilience = () => {
  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;
  const resilienceRef = useRef<StepsRef>(null);
  const { moveToNextPage, moveToPreviousPage } = useGeoPartitionNavigation();
  const {
    activeStep,
    geoPartitions,
    activeGeoPartitionIndex,
    universeData
  } = addGeoPartitionContext;
  const { updateGeoPartition, setActiveStep } = addGeoPartitionMethods;
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
              // we need to pass the updated context to next page.
              // using just context will have stale data and leads to race condition
              // as updateGeoPartition is async
              const updatedContext: AddGeoPartitionContextProps = {
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
              moveToNextPage(updatedContext);
            },
            moveToNextPage: () => {},
            moveToPreviousPage: () => moveToPreviousPage()
          }
        ] as unknown) as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{geoPartitions[activeGeoPartitionIndex].name}</>}
          subTitle={<>Resilience And Regions</>}
        />

        <ResilienceAndRegions isGeoPartition hideHelpText ref={resilienceRef} />
        <UniverseActionButtons
          cancelButton={{
            text: 'Cancel',
            onClick: () => {
              setActiveStep(activeStep - 1);
            }
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
