import {
  AVAILABILITY_ZONES_BY_PROVIDER,
  REPLICATION_FACTOR
} from '../../fields/FieldNames';

export type Zone = {
  name: string;
  uuid: string;
  nodeCount: number;
  preffered: number;
};
export interface NodeAvailabilityProps {
  availabilityZones: {
    [region: string]: Zone[];
  };
  /**
   * Multi provider form field (Guided + Expert). Persisted to nodesByProvider.
   * RF / useDedicatedNodes stay on the singular slice.
   */
  [AVAILABILITY_ZONES_BY_PROVIDER]?: {
    [providerUuid: string]: {
      [region: string]: Zone[];
    };
  };
  useDedicatedNodes: boolean;
  [REPLICATION_FACTOR]?: number;
}
