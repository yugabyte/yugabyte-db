import { createContext } from 'react';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../../create-universe/steps/resilence-regions/dtos';

export interface GeoPartition {
  name: string;
  tablespaceName: string;
  resilience?: ResilienceAndRegionsProps;
  nodesAndAvailability?: NodeAvailabilityProps;
}

export interface AddGeoPartitionContextProps {
  geoPartitions: GeoPartition[];
  activeGeoPartitionIndex: number;
  activeStep: number;
  isNewGeoPartition: boolean;
}

export enum AddGeoPartitionSteps {
  GENERAL_SETTINGS = 1,
  RESILIENCE_AND_REGIONS,
  NODES_AND_AVAILABILITY_ZONES,
  REVIEW
}

export const initialAddGeoPartitionFormState: AddGeoPartitionContextProps = {
  geoPartitions: [
    {
      name: 'Geo Partition 1',
      tablespaceName: 'Tablespace 1',
      resilience: {
        regions: [],
        faultToleranceType: FaultToleranceType.AZ_LEVEL,
        nodeCount: 1,
        replicationFactor: 3,
        resilienceFormMode: ResilienceFormMode.FREE_FORM,
        resilienceType: ResilienceType.REGULAR
      },
      nodesAndAvailability: {
        availabilityZones: {},
        nodeCountPerAz: 1,
        useDedicatedNodes: false
      }
    }
  ],
  activeGeoPartitionIndex: 0,
  activeStep: AddGeoPartitionSteps.GENERAL_SETTINGS,
  isNewGeoPartition: false
};

export const AddGeoPartitionContext = createContext<AddGeoPartitionContextProps>(
  initialAddGeoPartitionFormState
);

export const addGeoPartitionFormMethods = (context: AddGeoPartitionContextProps) => ({
  addGeoPartition: (geoPartition: GeoPartition) => ({
    ...context,
    geoPartitions: [...context.geoPartitions, geoPartition],
    activeGeoPartitionIndex: context.geoPartitions.length,
    activeStep: AddGeoPartitionSteps.GENERAL_SETTINGS
  }),
  updateGeoPartition: ({
    geoPartition,
    activeGeoPartitionIndex
  }: {
    geoPartition: GeoPartition;
    activeGeoPartitionIndex: number;
  }) => ({
    ...context,
    geoPartitions: context.geoPartitions.map((partition, i) =>
      i === activeGeoPartitionIndex ? geoPartition : partition
    )
  }),
  deleteGeoPartition: (geoPartition: GeoPartition) => ({
    ...context,
    geoPartitions: context.geoPartitions.filter((partition) => partition.name !== geoPartition.name)
  }),
  setActiveStep: (step: number) => ({
    ...context,
    activeStep: step
  }),
  setGeoPartitionContext: (context: AddGeoPartitionContextProps) => ({
    ...context
  }),
  setIsNewGeoPartition: (isNew: boolean) => ({
    ...context,
    isNewGeoPartition: isNew
  })
});

export type AddGeoPartitionContextMethods = [
  AddGeoPartitionContextProps,
  ReturnType<typeof addGeoPartitionFormMethods>
];
