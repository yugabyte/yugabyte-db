import { useContext, useRef } from 'react';
import { UniverseActionButtons } from '../../../create-universe/components/UniverseActionButtons';
import {
  CreateUniverseContext,
  createUniverseFormProps,
  StepsRef
} from '../../../create-universe/CreateUniverseContext';
import { NodesAvailability } from '../../../create-universe/steps';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  AddGeoPartitionContextProps,
  GeoPartition,
  initialAddGeoPartitionFormState
} from '../AddGeoPartitionContext';
import { YBButton, mui } from '@yugabyte-ui-library/core';
import { AddCircleOutline } from '@material-ui/icons';
import { getExistingGeoPartitions, useGeoPartitionNavigation } from '../AddGeoPartitionUtils';
import GeoPartitionBreadCrumb from '../GeoPartitionBreadCrumbs';

const { Box } = mui;

export const GeoPartitionNodesAndAvailability = () => {
  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;
  const {
    activeStep,
    geoPartitions,
    activeGeoPartitionIndex,
    universeData
  } = addGeoPartitionContext;
  const { setActiveStep, addGeoPartition, updateGeoPartition } = addGeoPartitionMethods;
  const nodesAndAvailabilityRef = useRef<StepsRef>(null);
  const { moveToNextPage, moveToPreviousPage } = useGeoPartitionNavigation();

  const alreadyExistingGeoParitionsCount = getExistingGeoPartitions(
    addGeoPartitionContext.universeData!
  ).length;

  return (
    <CreateUniverseContext.Provider
      value={
        ([
          {
            activeStep: 1,
            resilienceAndRegionsSettings: geoPartitions[activeGeoPartitionIndex].resilience,
            nodesAvailabilitySettings: geoPartitions[activeGeoPartitionIndex].nodesAndAvailability
          },
          {
            saveNodesAvailabilitySettings: (data: GeoPartition['nodesAndAvailability']) => {
              updateGeoPartition({
                geoPartition: {
                  ...geoPartitions[activeGeoPartitionIndex],
                  nodesAndAvailability: data
                },
                activeGeoPartitionIndex: activeGeoPartitionIndex
              });
              const updatedContext: AddGeoPartitionContextProps = {
                ...addGeoPartitionContext,
                geoPartitions: geoPartitions.map((gp, idx) =>
                  idx === activeGeoPartitionIndex
                    ? {
                        ...gp,
                        nodesAndAvailability: data
                      }
                    : gp
                )
              };
              moveToNextPage(updatedContext);
            },
            moveToNextPage: () => {}
          }
        ] as unknown) as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', gap: '24px', flexDirection: 'column' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{geoPartitions[activeGeoPartitionIndex].name}</>}
          subTitle={<>Nodes and Availability</>}
        />
        <NodesAvailability
          isGeoPartition
          ref={nodesAndAvailabilityRef}
          universeData={universeData}
        />
        <UniverseActionButtons
          prevButton={{
            text: 'Prev',
            onClick: moveToPreviousPage
          }}
          cancelButton={{
            text: 'Cancel',
            onClick: () => {
              setActiveStep(activeStep - 1);
            }
          }}
          nextButton={{
            text: 'Next',
            onClick: () => {
              nodesAndAvailabilityRef.current?.onNext();
            }
          }}
          additionalButtons={
            activeGeoPartitionIndex === geoPartitions.length - 1 ? (
              <YBButton
                variant="secondary"
                onClick={() => {
                  nodesAndAvailabilityRef.current?.onNext().then(() => {
                    addGeoPartition({
                      ...initialAddGeoPartitionFormState.geoPartitions[0],
                      name: `Geo Partition ${
                        alreadyExistingGeoParitionsCount + geoPartitions.length + 1
                      }`,
                      tablespaceName: `Tablespace ${
                        alreadyExistingGeoParitionsCount + geoPartitions.length + 1
                      }`
                    });
                  });
                }}
                dataTestId="add-new-geo-partition-button"
                size="large"
                startIcon={<AddCircleOutline />}
              >
                Add Another Geo Partition
              </YBButton>
            ) : undefined
          }
        />
      </Box>
    </CreateUniverseContext.Provider>
  );
};
