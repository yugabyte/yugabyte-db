import { useContext, useMemo } from 'react';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  AddGeoPartitionContextProps,
  AddGeoPartitionSteps
} from './AddGeoPartitionContext';
import { Step } from '@yugabyte-ui-library/core/dist/esm/components/YBMultiLevelStepper/YBMultiLevelStepper';

export function useGeoPartitionNavigation() {
  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;

  const { activeStep, geoPartitions, activeGeoPartitionIndex } = addGeoPartitionContext;
  const { setGeoPartitionContext } = addGeoPartitionMethods;
  const steps = useGetSteps(addGeoPartitionContext);

  function prev() {
    const MAX_STEP_COUNT = steps[Math.max(0, activeGeoPartitionIndex - 1)].subSteps.length;
    const context = {
      ...addGeoPartitionContext
    };
    if (activeStep === 1 && activeGeoPartitionIndex !== 0) {
      context.activeStep = MAX_STEP_COUNT;
      context.activeGeoPartitionIndex = activeGeoPartitionIndex - 1;
    } else {
      context.activeStep = Math.max(activeStep - 1, 1);
    }
    return setGeoPartitionContext(context);
  }

  function next() {
    const MAX_STEP_COUNT = steps[activeGeoPartitionIndex].subSteps.length;

    const context = {
      ...addGeoPartitionContext
    };

    if (activeStep < MAX_STEP_COUNT) {
      context.activeStep = activeStep + 1;
    } else if (activeStep === MAX_STEP_COUNT) {
      if (activeGeoPartitionIndex < geoPartitions.length - 1) {
        context.activeGeoPartitionIndex = activeGeoPartitionIndex + 1;
        context.activeStep = 1;
      } else {
        context.activeGeoPartitionIndex = geoPartitions.length;
        context.activeStep = AddGeoPartitionSteps.REVIEW;
      }
    }
    return setGeoPartitionContext(context);
  }
  return { moveToPreviousPage: prev, moveToNextPage: next };
}

export function useGetSteps(context: AddGeoPartitionContextProps): Step[] {
  const { geoPartitions, activeGeoPartitionIndex, isNewGeoPartition } = context;

  return useMemo(() => {
    const steps: Step[] = geoPartitions.map((geoPartition, index) => ({
      groupTitle: geoPartition.name,
      subSteps: [
        {
          title: 'General Settings'
        },
        ...(index !== 0 || !isNewGeoPartition
          ? [
              {
                title: 'Resilience and Regions'
              },
              {
                title: 'Nodes and Availability Zones'
              }
            ]
          : [])
      ]
    }));

    return [...steps, { groupTitle: 'Review', subSteps: [{ title: 'Summary and Cost' }] }];
  }, [geoPartitions, activeGeoPartitionIndex, isNewGeoPartition]);
}
