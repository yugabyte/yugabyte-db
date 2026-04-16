import {
  YBMaps,
  YBMapMarker,
  MarkerType,
  MapLegend,
  MapLegendItem,
  type useGetMapIcons
} from '@yugabyte-ui-library/core';
import { Region } from '../../../../../helpers/dtos';

type MapIcon = ReturnType<typeof useGetMapIcons>;

export function NodesAvailabilityMapSection({
  regions,
  icon
}: {
  regions: Region[];
  icon: MapIcon;
}) {
  return (
    <YBMaps
      mapHeight={216}
      coordinates={[
        [37.3688, -122.0363],
        [34.052235, -118.243683]
      ]}
      initialBounds={undefined}
      defaultZoom={5}
      dataTestId="yb-maps-nodes-availability"
      mapContainerProps={{
        scrollWheelZoom: false,
        zoom: 2,
        center: [0, 0]
      }}
    >
      {
        regions?.map((region: Region) => {
          return (
            <YBMapMarker
              key={region.code}
              position={[region.latitude, region.longitude]}
              type={MarkerType.REGION_SELECTED}
              tooltip={<>{region.name}</>}
            />
          );
        }) as any
      }
      {regions?.length > 0 ? (
        <MapLegend
          mapLegendItems={[
            <MapLegendItem key="region-legend" icon={<>{icon.normal}</>} label={'Region'} />
          ]}
        />
      ) : (
        <span />
      )}
    </YBMaps>
  );
}
