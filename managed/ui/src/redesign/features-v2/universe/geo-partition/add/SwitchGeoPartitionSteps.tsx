import { useContext } from 'react';
import { useMap } from 'react-use';
import { GeoPartitionGeneralSettings } from './steps/GeoPartitionGeneralSettings';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  AddGeoPartitionSteps
} from './AddGeoPartitionContext';
import { GeoPartitionResilience } from './steps/GeoPartitionResilience';
import { GeoPartitionNodesAndAvailability } from './steps/GeoPartitionNodesAndAvailability';

export const SwitchGeoPartitionSteps = () => {
  const [{ activeStep }] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;
  const mappings = {
    [AddGeoPartitionSteps.GENERAL_SETTINGS]: <GeoPartitionGeneralSettings />,
    [AddGeoPartitionSteps.RESILIENCE_AND_REGIONS]: <GeoPartitionResilience />,
    [AddGeoPartitionSteps.NODES_AND_AVAILABILITY_ZONES]: <GeoPartitionNodesAndAvailability />,
    [AddGeoPartitionSteps.REVIEW]: <div>Summary and Costs</div>
  };

  const [, { get }] = useMap<Record<number, JSX.Element>>(mappings);
  const getCurrentComponent = () => {
    return get(activeStep);
  };

  return getCurrentComponent() ?? null;
};
