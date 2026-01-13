import { useContext } from 'react';
import { EditPlacementContext, EditPlacementContextMethods } from './EditPlacementContext';
import { ClusterSpecClusterType, Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { Region } from '@app/redesign/helpers/dtos';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import {
  extractGeoPartitionsFromUniverse,
  getExistingGeoPartitions
} from '../../geo-partition/add/AddGeoPartitionUtils';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  mapUniversePayloadToResilienceAndRegionsProps
} from '../EditUniverseUtils';

export const useGetEditPlacementContext = (): EditPlacementContextMethods => {
  const context = useContext(EditPlacementContext);
  if (!context) {
    throw new Error('useGetEditPlacementContext must be used within an EditPlacementProvider');
  }
  return (context as unknown) as EditPlacementContextMethods;
};

export const getResilienceAndRegionsProps = (
  universeData: Universe,
  providerRegions: Region[],
  selectedPartitionUUID?: string
): ResilienceAndRegionsProps => {
  const hasGeoPartitions = getExistingGeoPartitions(universeData!).length > 0;
  const primaryCluster = getClusterByType(universeData, ClusterSpecClusterType.PRIMARY);
  if (hasGeoPartitions) {
    const selectedPartition = primaryCluster?.partitions_spec?.find(
      (partition) => partition.uuid === selectedPartitionUUID
    );
    const stats = countRegionsAzsAndNodes(selectedPartition!.placement!);
    const props = mapUniversePayloadToResilienceAndRegionsProps(
      providerRegions!,
      stats,
      selectedPartition!
    );

    const geoPartitions = extractGeoPartitionsFromUniverse(universeData, providerRegions);
    props['regions'] = [
      geoPartitions.regions.find((region) => region.partitionUUID === selectedPartitionUUID)!
    ];
    return props;
  } else {
    const stats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);
    return mapUniversePayloadToResilienceAndRegionsProps(providerRegions!, stats, primaryCluster!);
  }
};
