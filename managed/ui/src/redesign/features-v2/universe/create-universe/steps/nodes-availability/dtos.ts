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
  useDedicatedNodes: boolean;
  nodeCountPerAz: number;
}
