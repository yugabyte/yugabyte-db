import { ReactNode, useContext } from 'react';
import { mui } from '@yugabyte-ui-library/core';
import { NodeInstanceDetails } from '../../../geo-partition/add/NodeInstanceDetails';
import { getClusterByType } from '../../../edit-universe/EditUniverseUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { AddGeoPartitionContext } from '../../../geo-partition/add/AddGeoPartitionContext';
import { EditUniverseContext } from '../../../edit-universe/EditUniverseContext';

const { Box } = mui;

const getGeoUniverseData = (contextValue: unknown) => {
  if (Array.isArray(contextValue)) {
    return (contextValue[0] as { universeData?: unknown })?.universeData;
  }
  return (contextValue as { universeData?: unknown })?.universeData;
};

export type GuidedNodesAvailabilityLayoutSlots = {
  map: ReactNode;
  requirementCard: ReactNode;
  availabilityZones: ReactNode;
  lesserNodesAlert: ReactNode;
  dedicatedNode?: ReactNode;
};

/** Create-universe guided nodes: map first, then form stack (current behavior). */
export function GuidedNodesAvailabilityDefaultLayout({
  map,
  requirementCard,
  availabilityZones,
  lesserNodesAlert,
  dedicatedNode
}: GuidedNodesAvailabilityLayoutSlots) {
  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
      {map}
      {requirementCard}
      {availabilityZones}
      {lesserNodesAlert}
      {dedicatedNode}
    </Box>
  );
}

/** Add geo partition guided nodes: form stack left, map right (Figma Geo-AZ). */
export function GuidedNodesAvailabilityGeoLayout({
  map,
  requirementCard,
  availabilityZones,
  lesserNodesAlert
}: Omit<GuidedNodesAvailabilityLayoutSlots, 'dedicatedNode'>) {
  const addGeoPartitionContext = useContext(AddGeoPartitionContext);
  const universeData = getGeoUniverseData(addGeoPartitionContext);
  const { universeData: editUniverseData } = useContext(EditUniverseContext);
  const resolvedUniverseData = universeData ?? editUniverseData;

  if (!resolvedUniverseData) return null;

  const cluster = getClusterByType(resolvedUniverseData, ClusterSpecClusterType.PRIMARY);
  
  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'row',
        gap: '24px',
        alignItems: 'flex-start'
      }}
    >
      <Box
        sx={{
          flex: '1 1 auto',
          maxWidth: 720,
          minWidth: 0,
          display: 'flex',
          flexDirection: 'column',
          gap: '24px'
        }}
      >
        {requirementCard}
        {availabilityZones}
        {lesserNodesAlert}
      </Box>
      <Box sx={{ flex: '0 0 360px', width: 360, minWidth: 360, gap: '16px', display: 'flex', flexDirection: 'column' }}>
        {map}
        {cluster && <NodeInstanceDetails cluster={cluster} />}
      </Box>
    </Box>
  );
}
