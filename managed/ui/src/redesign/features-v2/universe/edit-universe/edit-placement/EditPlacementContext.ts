import { createContext } from 'react';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';

export enum EditPlacementSteps {
  RESILIENCE_AND_REGIONS = 1,
  NODES_AND_AVAILABILITY_ZONES
}

export interface EditPlacementContextProps {
  activeStep: EditPlacementSteps;
  resilience?: ResilienceAndRegionsProps;
  nodesAndAvailability?: NodeAvailabilityProps;
}

export const EditPlacementContext = createContext<EditPlacementContextProps>({
  activeStep: EditPlacementSteps.RESILIENCE_AND_REGIONS
});

export const editPlacementMethods = (context: EditPlacementContextProps) => ({
  setActiveStep: (activeStep: number) => ({
    ...context,
    activeStep
  }),
  setResilience: (resilience: ResilienceAndRegionsProps) => ({
    ...context,
    resilience
  }),
  setNodesAndAvailability: (nodesAndAvailability: NodeAvailabilityProps) => ({
    ...context,
    nodesAndAvailability
  }),
  setSelectedPartitionUUID: (selectedPartitionUUID: string) => ({
    ...context,
    selectedPartitionUUID
  }),
  resetContext: () => ({
    activeStep: EditPlacementSteps.RESILIENCE_AND_REGIONS,
    resilience: undefined,
    nodesAndAvailability: undefined
  })
});

export type EditPlacementContextMethods = [
  EditPlacementContextProps,
  ReturnType<typeof editPlacementMethods>,
  {
    hideModal: () => void;
    selectedPartitionUUID?: string;
    onSubmit: (ctx: EditPlacementContextProps) => void;
  }
];
