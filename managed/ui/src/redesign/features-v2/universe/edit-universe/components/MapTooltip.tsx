import { FC } from 'react';
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

  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse' });
  const { universeData } = useEditUniverseContext();
  const isK8s = isKubernetesUniverse(universeData!);
  return (
    <StyledTooltipContainer>
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
