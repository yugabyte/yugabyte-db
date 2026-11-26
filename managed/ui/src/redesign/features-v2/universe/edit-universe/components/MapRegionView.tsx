import { FC } from 'react';
import { ClusterType } from '@app/redesign/helpers/dtos';
import { groupBy } from 'lodash';
import pluralize from 'pluralize';
import { useTranslation } from 'react-i18next';
import {
  YBMapMarker,
  MarkerType,
  MapLegend,
  MapLegendItem,
  useGetMapIcons
} from '@yugabyte-ui-library/core';
import { RegionsAndNodesFormType } from '../../geo-partition/add/AddGeoPartitionUtils';
import { MapRegionTooltip } from './MapTooltip';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  isKubernetesUniverse,
  useEditUniverseContext
} from '../EditUniverseUtils';

interface MapRegionsViewProps {
  regions: RegionsAndNodesFormType['regions'];
}
export const MapRegionsView: FC<MapRegionsViewProps> = ({ regions }) => {
  const { universeData } = useEditUniverseContext();
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.general' });
  const unit = t(isKubernetesUniverse(universeData!) ? 'pod' : 'node');

  const regionsByName = groupBy(regions, 'code');
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });
  const readReplicaIcon = useGetMapIcons({ type: MarkerType.READ_REPLICA });

  const primaryCluster = getClusterByType(universeData!, ClusterType.PRIMARY);
  const asyncCluster = getClusterByType(universeData!, ClusterType.ASYNC);
  const primaryRegionStats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);
  const readReplicaRegionStats = asyncCluster
    ? countRegionsAzsAndNodes(asyncCluster!.placement_spec!)
    : null;

  return (
    <>
      {regions?.map((region) => {
        return (
          <YBMapMarker
            key={region.code}
            position={[region.latitude, region.longitude]}
            type={
              region.clusterType === ClusterType.ASYNC
                ? MarkerType.READ_REPLICA
                : MarkerType.REGION_SELECTED
            }
            tooltip={<MapRegionTooltip regions={regionsByName[region.code]} />}
          />
        );
      })}
      <MapLegend
        mapLegendItems={[
          <MapLegendItem
            icon={<>{icon.normal}</>}
            label={t('region')}
            subText={`${primaryRegionStats.totalRegions} ${pluralize(
              t('region'),
              primaryRegionStats.totalRegions
            )}, ${primaryRegionStats.totalAzs} ${pluralize(
              t('az'),
              primaryRegionStats.totalAzs
            )}, ${primaryRegionStats.totalNodes} ${pluralize(unit, primaryRegionStats.totalNodes)}`}
          />,
          asyncCluster ? (
            <MapLegendItem
              icon={<>{readReplicaIcon.normal}</>}
              label={t('readReplica')}
              subText={`${readReplicaRegionStats?.totalRegions} ${pluralize(
                t('region'),
                readReplicaRegionStats?.totalRegions
              )}, ${readReplicaRegionStats?.totalAzs} ${pluralize(
                t('az'),
                readReplicaRegionStats?.totalAzs
              )}, ${readReplicaRegionStats?.totalNodes} ${pluralize(
                unit,
                readReplicaRegionStats?.totalNodes
              )}`}
            />
          ) : (
            <></>
          )
        ]}
      />
    </>
  );
};
