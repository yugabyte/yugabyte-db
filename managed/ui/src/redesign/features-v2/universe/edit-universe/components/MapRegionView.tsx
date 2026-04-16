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
import { isDefinedNotNull } from '@app/utils/ObjectUtils';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  useEditUniverseContext
} from '../EditUniverseUtils';

type ZoneType = RegionsAndNodesFormType['regions'][number]['zones'][number];

interface MapRegionsViewProps {
  regions: RegionsAndNodesFormType['regions'];
}
export const MapRegionsView: FC<MapRegionsViewProps> = ({ regions }) => {
  const { universeData } = useEditUniverseContext();

  const regionsByName = groupBy(regions, 'code');
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.general' });
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });
  const readReplicaIcon = useGetMapIcons({ type: MarkerType.READ_REPLICA });
  const preferedIcon = useGetMapIcons({ type: MarkerType.REGION_PREFERRED });

  const primaryCluster = getClusterByType(universeData!, ClusterType.PRIMARY);
  const asyncCluster = getClusterByType(universeData!, ClusterType.ASYNC);
  const primaryRegionStats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);
  const readReplicaRegionStats = asyncCluster
    ? countRegionsAzsAndNodes(asyncCluster!.placement_spec!)
    : null;

  const hasPrefferedRegions = regions.some((region) =>
    region.zones.some(
      (zone: ZoneType) => isDefinedNotNull(zone.leader_preference) && zone.leader_preference! >= 0
    )
  );

  return (
    <>
      {regions?.map((region) => {
        const hasHighestPreferedRank = region?.zones?.some(
          (zone: ZoneType) =>
            isDefinedNotNull(zone.leader_preference) && zone.leader_preference === 0
        );
        return (
          <YBMapMarker
            key={region.code}
            position={[region.latitude, region.longitude]}
            type={
              region.clusterType === ClusterType.ASYNC
                ? MarkerType.READ_REPLICA
                : hasHighestPreferedRank
                ? MarkerType.REGION_PREFERRED
                : MarkerType.READ_REPLICA
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
            )}, ${primaryRegionStats.totalAzs} ${pluralize('AZ', primaryRegionStats.totalAzs)}, ${
              primaryRegionStats.totalNodes
            } ${pluralize('node', primaryRegionStats.totalNodes)}`}
          />,
          asyncCluster ? (
            <MapLegendItem
              icon={<>{readReplicaIcon.normal}</>}
              label={t('readReplica')}
              subText={`${readReplicaRegionStats?.totalRegions} ${pluralize(
                'region',
                readReplicaRegionStats?.totalRegions
              )}, ${readReplicaRegionStats?.totalAzs} ${pluralize(
                'AZ',
                readReplicaRegionStats?.totalAzs
              )}s, ${readReplicaRegionStats?.totalNodes} ${pluralize(
                'node',
                readReplicaRegionStats?.totalNodes
              )}`}
            />
          ) : (
            <></>
          ),
          hasPrefferedRegions ? (
            <MapLegendItem icon={<>{preferedIcon.normal}</>} label={t('preferredRank1')} />
          ) : (
            <></>
          )
        ]}
      />
    </>
  );
};
