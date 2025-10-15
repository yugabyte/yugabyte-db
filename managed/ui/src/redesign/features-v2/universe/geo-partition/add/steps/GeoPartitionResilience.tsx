import { useContext } from 'react';
import { AddGeoPartitionContext, AddGeoPartitionContextMethods } from '../AddGeoPartitionContext';
import {
  CreateUniverseContext,
  createUniverseFormProps
} from '../../../create-universe/CreateUniverseContext';
import { ResilienceAndRegions } from '../../../create-universe/steps';
import { UniverseActionButtons } from '../../../create-universe/components/UniverseActionButtons';
import { useGeoPartitionNavigation } from '../AddGeoPartitionUtils';
import GeoPartitionBreadCrumb from '../GeoPartitionBreadCrumbs';
import { mui } from '@yugabyte-ui-library/core';

const { Box } = mui;

export const GeoPartitionResilience = () => {
  const [{ activeStep, geoPartitions, activeGeoPartitionIndex }, { setActiveStep }] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;

  const { moveToNextPage, moveToPreviousPage } = useGeoPartitionNavigation();

  return (
    <CreateUniverseContext.Provider
      value={
        ([
          {
            activeStep: 1,
            resilienceAndRegionsSettings: geoPartitions[activeGeoPartitionIndex].resilience
          },
          {
            setResilienceType: () => {}
          }
        ] as unknown) as createUniverseFormProps
      }
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <GeoPartitionBreadCrumb
          groupTitle={<>{geoPartitions[activeGeoPartitionIndex].name}</>}
          subTitle={<>Resilience And Regions</>}
        />

        <ResilienceAndRegions />
        <UniverseActionButtons
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
          prevButton={{
            onClick: moveToPreviousPage
          }}
        />
      </Box>
    </CreateUniverseContext.Provider>
  );
};
