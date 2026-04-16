import { useContext } from 'react';
import { UniverseActionButtons } from '../../../create-universe/components/UniverseActionButtons';
import {
  CreateUniverseContext,
  createUniverseFormProps
} from '../../../create-universe/CreateUniverseContext';
import { NodesAvailability } from '../../../create-universe/steps';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  initialAddGeoPartitionFormState
} from '../AddGeoPartitionContext';
import { YBButton, mui } from '@yugabyte-ui-library/core';
import { AddCircleOutline } from '@material-ui/icons';
import { useGeoPartitionNavigation } from '../AddGeoPartitionUtils';
import GeoPartitionBreadCrumb from '../GeoPartitionBreadCrumbs';

const { Box } = mui;

export const GeoPartitionNodesAndAvailability = () => {
  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;
  const { activeStep, geoPartitions, activeGeoPartitionIndex } = addGeoPartitionContext;
  const { setActiveStep, addGeoPartition } = addGeoPartitionMethods;

  const { moveToNextPage, moveToPreviousPage } = useGeoPartitionNavigation();

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
            saveNodesAvailabilitySettings: () => {}
          }
        ] as unknown) as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', gap: '24px', flexDirection: 'column' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{geoPartitions[activeGeoPartitionIndex].name}</>}
          subTitle={<>Nodes and Availability</>}
        />
        <NodesAvailability />
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
            onClick: moveToNextPage
          }}
          additionalButtons={
            <YBButton
              variant="secondary"
              onClick={() => {
                addGeoPartition({
                  ...initialAddGeoPartitionFormState.geoPartitions[0],
                  name: `Geo Partition ${geoPartitions.length + 1}`,
                  tablespaceName: 'Tablespace 1'
                });
              }}
              dataTestId="add-new-geo-partition-button"
              size="large"
              startIcon={<AddCircleOutline />}
            >
              Add Another Geo Partition
            </YBButton>
          }
        />
      </Box>
    </CreateUniverseContext.Provider>
  );
};
