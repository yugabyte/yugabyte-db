import { FC, RefObject, useLayoutEffect, useRef } from 'react';
import { groupBy } from 'lodash';
import { useTranslation } from 'react-i18next';
import { TFunction } from 'i18next';
import { IconPosition, mui, StatusType, YBSmartStatus } from '@yugabyte-ui-library/core';

import { RegionsAndNodesFormType } from '../../geo-partition/add/AddGeoPartitionUtils';
import { isDefinedNotNull } from '@app/utils/ObjectUtils';
import { ClusterSpecClusterType, PlacementAZ } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import './MapTooltip.css';
import { AZ_NOT_PREFERRED, AZ_PREFFERED_HIGHEST_RANK } from '../../create-universe/helpers/constants';
import { isKubernetesUniverse, useEditUniverseContext } from '../EditUniverseUtils';

const { styled, Typography, Divider } = mui;

interface MapRegionTooltipProps {
  regions: RegionsAndNodesFormType['regions'];
  partitionName?: string;
}

/** Gap kept between the tooltip and the map edges. */
const MAP_EDGE_PADDING = 8;

/**
 * `.leaflet-container` is `overflow: hidden`, so tooltips taller than the space above
 * their marker get cut off. Leaflet has no auto-pan for tooltips, so shift its tooltip
 * element back inside the map viewport. The shift uses the `translate` property, which
 * composes with the `transform` Leaflet writes when it positions the tooltip.
 */
const useKeepTooltipWithinMap = (contentRef: RefObject<HTMLDivElement>) => {
  useLayoutEffect(() => {
    const tooltip = contentRef.current?.closest<HTMLElement>('.leaflet-tooltip');
    const map = tooltip?.closest<HTMLElement>('.leaflet-container');
    if (!tooltip || !map) return;

    let offsetX = 0;
    let offsetY = 0;

    const clampIntoView = () => {
      const tooltipRect = tooltip.getBoundingClientRect();
      const mapRect = map.getBoundingClientRect();

      // Undo the applied shift to get the position Leaflet asked for.
      const top = tooltipRect.top - offsetY;
      const bottom = tooltipRect.bottom - offsetY;
      const left = tooltipRect.left - offsetX;
      const right = tooltipRect.right - offsetX;

      const minTop = mapRect.top + MAP_EDGE_PADDING;
      const maxBottom = mapRect.bottom - MAP_EDGE_PADDING;
      const minLeft = mapRect.left + MAP_EDGE_PADDING;
      const maxRight = mapRect.right - MAP_EDGE_PADDING;

      let nextOffsetY = 0;
      if (top < minTop) {
        nextOffsetY = minTop - top;
      } else if (bottom > maxBottom) {
        // Never push the top out of view for tooltips taller than the map.
        nextOffsetY = Math.max(maxBottom - bottom, minTop - top);
      }

      let nextOffsetX = 0;
      if (left < minLeft) {
        nextOffsetX = minLeft - left;
      } else if (right > maxRight) {
        nextOffsetX = Math.max(maxRight - right, minLeft - left);
      }

      if (nextOffsetX === offsetX && nextOffsetY === offsetY) return;

      offsetX = nextOffsetX;
      offsetY = nextOffsetY;
      tooltip.style.setProperty('translate', `${offsetX}px ${offsetY}px`);
    };

    clampIntoView();

    // Leaflet re-positions the tooltip on pan/zoom by rewriting its inline style.
    const observer = new MutationObserver(clampIntoView);
    observer.observe(tooltip, { attributes: true, attributeFilter: ['style', 'class'] });
    window.addEventListener('resize', clampIntoView);

    return () => {
      observer.disconnect();
      window.removeEventListener('resize', clampIntoView);
      tooltip.style.removeProperty('translate');
    };
  }, [contentRef]);
};

const StyledTooltipContainer = styled('div')(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  padding: '6px 8px',
  gap: '6px',
  width: 'fit-content',
  borderRadius: '8px',
  boxShadow: `0 0 7px 0 rgba(153, 153, 153, 0.25)`,
  background: theme.palette.common.white,
  zIndex: 1001
}));

const StyledHeader = styled(Typography)(({ theme }) => ({
  fontSize: '12px',
  fontWeight: 500,
  color: theme.palette.grey[700],
  padding: '4px 0px'
}));
const ZoneContainer = styled('ul')(() => ({
  margin: 0,
  marginLeft: '8px',
  padding: 0
}));
const ZoneItem = styled('li')<{ preferredRank?: number }>(({ preferredRank }) => ({
  display: 'flex',
  flexDirection: 'row',
  alignItems: 'center',
  gap: '8px',
  fontSize: '13px',
  fontWeight: 600,
  height: '16px',
  padding: '14px 0px',
  color: preferredRank === AZ_PREFFERED_HIGHEST_RANK ? '#BB42BC' : '#735AF5',
  '&::before': {
    content: '""',
    display: 'block',
    width: '4px',
    height: '4px',
    backgroundColor: preferredRank === AZ_PREFFERED_HIGHEST_RANK ? '#BB42BC' : '#735AF5',
    borderRadius: '50%'
  }
}));

const StyledNodeCount = StyledHeader;

const StyledDivider = styled(Divider)(({ theme }) => ({
  color: theme.palette.grey[300],
  height: '1px'
}));

const RegionList: FC<{
  regions: RegionsAndNodesFormType['regions'];
  t: TFunction;
  isK8s: boolean;
}> = ({ regions, t, isK8s }) => {
  return (
    <>
      {regions?.map((region) => {
        const sortedZones = [...(region?.zones ?? [])].sort((a, b) => {
          const prefA = isDefinedNotNull(a.leader_preference)
            ? a.leader_preference! !== AZ_NOT_PREFERRED
              ? a.leader_preference!
              : Number.MAX_VALUE
            : Number.MAX_VALUE;
          const prefB = isDefinedNotNull(b.leader_preference)
            ? b.leader_preference! !== AZ_NOT_PREFERRED
              ? b.leader_preference!
              : Number.MAX_VALUE
            : Number.MAX_VALUE;
          return prefA - prefB;
        });
        return sortedZones.map(
          (zone: RegionsAndNodesFormType['regions'][number]['zones'][number]) => (
            <ZoneContainer key={zone.code}>
              <ZoneItem preferredRank={zone.leader_preference}>
                <Typography variant="body2" sx={{ width: '126px' }}>
                  {zone.name}
                </Typography>
                <StyledNodeCount>
                  {t(isK8s ? 'totalPods' : 'totalNodes', {
                    total: (zone as PlacementAZ).num_nodes_in_az ?? 0
                  })}
                </StyledNodeCount>
                {isDefinedNotNull(zone.leader_preference) && zone.leader_preference! > AZ_NOT_PREFERRED && (
                  <YBSmartStatus
                    type={StatusType.OTHER}
                    label={t('preferredRank', { rank: zone.leader_preference! })}
                    iconPosition={IconPosition.NONE}
                  />
                )}
                {(region as any).clusterType === ClusterSpecClusterType.ASYNC && (
                  <YBSmartStatus
                    type={StatusType.OTHER}
                    label={t('readReplica')}
                    iconPosition={IconPosition.NONE}
                  />
                )}
              </ZoneItem>
            </ZoneContainer>
          )
        );
      })}
    </>
  );
};

export const MapRegionTooltip: FC<MapRegionTooltipProps> = ({ regions, partitionName }) => {
  const regionsByType = groupBy(regions, 'clusterType');
  const contentRef = useRef<HTMLDivElement>(null);

  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse' });
  const { universeData } = useEditUniverseContext();
  const isK8s = isKubernetesUniverse(universeData!);

  useKeepTooltipWithinMap(contentRef);

  return (
    <StyledTooltipContainer ref={contentRef}>
      {partitionName && (
        <>
          <StyledHeader>{partitionName}</StyledHeader>
          <StyledDivider />
        </>
      )}
      <StyledHeader>
        {regions[0]?.name} ({regions[0]?.code})
      </StyledHeader>
      <RegionList
        regions={regionsByType[ClusterSpecClusterType.PRIMARY] ?? []}
        t={t}
        isK8s={isK8s}
      />
      {regionsByType[ClusterSpecClusterType.ASYNC] && <StyledDivider />}
      <RegionList
        regions={regionsByType[ClusterSpecClusterType.ASYNC] ?? []}
        t={t}
        isK8s={isK8s}
      />
    </StyledTooltipContainer>
  );
};
