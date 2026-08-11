import {
  MapLegend,
  MapLegendItem,
  MarkerType,
  mui,
  useGetMapIcons,
  YBMapMarker
} from '@yugabyte-ui-library/core';
import { groupBy, toUpper } from 'lodash';
import pluralize from 'pluralize';
import { extractGeoPartitionsFromUniverse } from '../../geo-partition/add/AddGeoPartitionUtils';
import { useEditUniverseContext } from '../EditUniverseUtils';
import { useTranslation } from 'react-i18next';
import { isDefinedNotNull } from '@app/utils/ObjectUtils';
import { MapRegionTooltip } from './MapTooltip';

const { styled, Box } = mui;

interface MapMarkerProps {
  children?: React.ReactNode;
  hover: boolean;
  hasPreferredRegion?: boolean;
}
const MarkerText = styled('span')(() => ({
  position: 'absolute',
  top: '50%',
  left: '50%',
  transform: 'translate(-50%, -50%)',
  color: '#FFFFFF',
  fontSize: '12px',
  fontWeight: '600'
}));

export const MapMarker: React.FC<MapMarkerProps> = ({ children, hover, hasPreferredRegion }) => {
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });
  const preferedIcon = useGetMapIcons({ type: MarkerType.REGION_PREFERRED });
  return (
    <div>
      {hasPreferredRegion && (hover ? preferedIcon.hover : preferedIcon.normal)}
      {!hasPreferredRegion && (hover ? icon.hover : icon.normal)}
      <MarkerText>{children}</MarkerText>
    </div>
  );
};

export const MapGeoPartitionView = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const { regions } = extractGeoPartitionsFromUniverse(universeData!, providerRegions!);
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.general' });
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });
  const preferedIcon = useGetMapIcons({ type: MarkerType.REGION_PREFERRED });

  const regionCout = regions.length;
  const azCount = regions.reduce((acc, region) => acc + (region.zones?.length ?? 0), 0);
  const nodeCount = regions.reduce((acc, region) => {
    const regionNodeCount =
      region?.zones?.reduce((zoneAcc, zone) => zoneAcc + ((zone as any).num_nodes_in_az ?? 0), 0) ??
      0;
    return acc + regionNodeCount;
  }, 0);

  const regionsByName = groupBy(regions, 'code');
  const hasPrefferedRegions = regions.some((region) =>
    region.zones.some(
      (zone: any) => isDefinedNotNull(zone.leader_preference) && zone.leader_preference! >= 0
    )
  );
  return (
    <>
      {regions?.map((region, index) => {
        const hasHighestPreferedRank = region.zones.some(
          (zone: any) => isDefinedNotNull(zone.leader_preference) && zone.leader_preference === 0
        );
        return (
          <YBMapMarker
            key={region.code}
            position={[region.latitude, region.longitude]}
            icon={
              <MapMarker hasPreferredRegion={hasHighestPreferedRank} hover={false}>
                {String.fromCharCode(65 + index)}
              </MapMarker>
            }
            iconHover={
              <MapMarker hasPreferredRegion={hasHighestPreferedRank} hover={true}>
                {String.fromCharCode(65 + index)}
              </MapMarker>
            }
            tooltip={
              <MapRegionTooltip
                regions={regionsByName[region.code]}
                partitionName={toUpper((region as any).paritition_name)}
              />
            }
          />
        );
      })}
      <MapLegend
        mapLegendItems={[
          <MapLegendItem
            icon={<>{icon.normal}</>}
            label={t('region')}
            subText={`${regionCout} ${pluralize(t('region'), regionCout)}, ${azCount} ${pluralize(
              'AZ',
              azCount
            )}, ${nodeCount} ${pluralize('node', nodeCount)}`}
          />,
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
