import { useState, type MouseEvent, type ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';
import { useQuery } from 'react-query';
import { fetchProviderList } from '@app/api/admin';

import { mui, YBMaps, YBSelect, YBTag } from '@yugabyte-ui-library/core';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  extractGeoPartitionsFromUniverse,
  extractRegionsAndNodeDataFromUniverse
} from '../../geo-partition/add/AddGeoPartitionUtils';
import { StyledInfoRowNew } from '../../create-universe/components/DefaultComponents';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { ybFormatDate } from '@app/redesign/helpers/DateUtils';
import {
  countMasterAndTServerNodes,
  countRegionsAzsAndNodes,
  getClusterByType,
  getProviderIcon,
  getResilientType,
  hasDedicatedNodesForCluster,
  isKubernetesUniverse,
  useEditUniverseContext
} from '../EditUniverseUtils';
import { EditUniverseTabs } from '../EditUniverseContext';
import { getEditUniverseSettingsRoute } from '../editUniverseTabUtils';

import { getFlagFromRegion } from '../../create-universe/helpers/RegionToFlagUtils';
import { LinuxVersion } from '../components';
import { MapRegionsView } from '../components/MapRegionView';
import { useYBToast } from '../../create-universe/helpers/ToastUtils';
import { PROVIDER_TYPES } from '@app/config';
import { Star } from '@material-ui/icons';
import CopyIcon from '../../../../assets/copy_blue.svg';
import TreeIcon from '@app/redesign/assets/tree-icon.svg';

const { Box, styled, Typography, Grid2, Divider, MenuItem } = mui;

const StyledArea = styled('div')(({ theme }) => ({
  padding: '16px',
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`,
  display: 'flex',
  gap: '24px',
  flexDirection: 'column',
  background: theme.palette.common.white
}));

const StyledGeneralInfoNew = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  width: '100%',
  gap: '40px'
}));

const StyledCardHeader = styled(Typography)(({ theme }) => ({
  lineHeight: '20px',
  fontSize: '15px',
  fontWeight: 600,
  fontStyle: 'normal',
  color: theme.palette.common.black
}));

const StyledSection = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '24px',
  width: '100%'
}));

const StyledClusterSubsection = styled(Box)(() => ({
  display: 'flex',
  gap: '16px',
  alignItems: 'flex-start',
  width: '100%'
}));

const StyledClusterTitle = styled(Typography)(({ theme }) => ({
  fontSize: '13px',
  fontWeight: 600,
  lineHeight: '16px',
  color: theme.palette.grey[600]
}));

const StyledYBSelect = styled(YBSelect)(() => ({
  zIndex: 1000,
  margin: '8px',
  width: '200px',
  height: '32px'
}));

const ViewMoreLink = styled('a')(({ theme }) => ({
  color: theme.palette.primary[600],
  fontSize: '13px',
  fontWeight: 400,
  lineHeight: '16px',
  textDecoration: 'underline',
  textDecorationStyle: 'solid',
  textUnderlinePosition: 'from-font',
  marginLeft: '40px',
  cursor: 'pointer'
}));

enum MapViewMode {
  REGIONS = 'regions',
  GEO_PARTITIONS = 'geo-partitions'
}

const MAP_COORDINATES: [number, number][] = [
  [0, 0],
  [0, 0]
];

const MAP_CONTAINER_PROPS = {
  scrollWheelZoom: false,
  zoom: 2,
  center: [0, 0] as [number, number]
};

const ClusterSubsection = ({ title, children }: { title: string; children: ReactNode }) => (
  <StyledClusterSubsection>
    <TreeIcon style={{ width: 24, height: 24, flexShrink: 0 }} />
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px', minWidth: 0, flex: 1 }}>
      <StyledClusterTitle>{title}</StyledClusterTitle>
      {children}
    </Box>
  </StyledClusterSubsection>
);

const RegionTags = ({
  regions,
  defaultRegionUuid
}: {
  regions: Region[];
  defaultRegionUuid?: string;
}) => (
  <Box sx={{ display: 'flex', gap: '8px', flexWrap: 'wrap', alignItems: 'center' }}>
    {regions.map((region: Region) => (
      <YBTag
        key={region.uuid ?? region.code}
        variant="light"
        size="large"
        endIcon={region.uuid === defaultRegionUuid ? <Star /> : undefined}
      >
        {getFlagFromRegion(region.code)} {region.name} ({region.code})
      </YBTag>
    ))}
  </Box>
);

export const GeneralTab = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.general' });
  const isK8s = isKubernetesUniverse(universeData!);
  const toast = useYBToast();
  const [mapViewMode, setMapViewMode] = useState<MapViewMode>(MapViewMode.REGIONS);
  const r = extractRegionsAndNodeDataFromUniverse(universeData!, providerRegions!);
  const geoParitionsData = extractGeoPartitionsFromUniverse(universeData!, providerRegions!);
  const isGeoPartitionPresent = geoParitionsData.regions.length > 0;

  const encryptionAtTransitEnabled =
    universeData?.spec?.encryption_in_transit_spec?.enable_client_to_node_encrypt ??
    universeData?.spec?.encryption_in_transit_spec?.enable_node_to_node_encrypt;
  const encryptionAtRestEnabled = !!universeData?.spec?.encryption_at_rest_spec?.kms_config_uuid;

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const readReplicaCluster = getClusterByType(universeData!, ClusterSpecClusterType.ASYNC);

  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const providerName = PROVIDER_TYPES.find((provider) => provider.code === providerCode)?.name;
  const providerIcon = getProviderIcon(providerCode);

  let totalNodesCount = 0;

  if (!hasDedicatedNodesForCluster(universeData!, primaryCluster)) {
    const primaryRegionStats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);
    const readReplicaRegionStats = countRegionsAzsAndNodes(readReplicaCluster?.placement_spec);
    totalNodesCount = primaryRegionStats.totalNodes + readReplicaRegionStats.totalNodes;
  } else {
    const primaryTServerMasterCount = countMasterAndTServerNodes(universeData!, primaryCluster);
    const readReplicaTServerMasterCount = countMasterAndTServerNodes(
      universeData!,
      readReplicaCluster
    );
    totalNodesCount =
      primaryTServerMasterCount.TSERVER! +
      primaryTServerMasterCount.MASTER! +
      readReplicaTServerMasterCount.TSERVER! +
      readReplicaTServerMasterCount.MASTER!;
  }

  const { data: providers } = useQuery(['providers'], () => fetchProviderList(), {
    select: (data) => data.data
  });

  const currentProvider = providers?.find(
    (provider) => provider.uuid === primaryCluster?.provider_spec?.provider
  );

  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const placementRoute = getEditUniverseSettingsRoute(universeUuid, EditUniverseTabs.PLACEMENT);
  const hardwareRoute = getEditUniverseSettingsRoute(universeUuid, EditUniverseTabs.HARDWARE);

  const navigateToSettingsTab = (route: string) => (event: MouseEvent<HTMLAnchorElement>) => {
    event.preventDefault();
    browserHistory.push(route);
  };

  const primaryRegions = r.regions.filter(
    (region: Region) => (region as any).clusterType === ClusterSpecClusterType.PRIMARY
  );
  const readReplicaRegions = r.regions.filter(
    (region: Region) => (region as any).clusterType === ClusterSpecClusterType.ASYNC
  );

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px', width: '100%' }}>
      <YBMaps
        dataTestId="yb-edit-universe-regions"
        mapHeight={345}
        coordinates={MAP_COORDINATES}
        initialBounds={undefined}
        mapContainerProps={MAP_CONTAINER_PROPS}
        showBoundaries={false}
      >
        {/* {isGeoPartitionPresent && (
          <StyledYBSelect
            dataTestId="yb-select"
            value={mapViewMode}
            onChange={(e) => setMapViewMode(e.target.value as MapViewMode)}
          >
            <MenuItem value={MapViewMode.REGIONS}>{t('allLocation')}</MenuItem>
            <Divider />
            <MenuItem value={MapViewMode.GEO_PARTITIONS}>{t('geoPartition')}</MenuItem>
          </StyledYBSelect>
        )} */}
        {mapViewMode === MapViewMode.REGIONS && ((<MapRegionsView regions={r.regions} />) as any)}
        {/* {mapViewMode === MapViewMode.GEO_PARTITIONS && <MapGeoPartitionView />} */}
      </YBMaps>
      <StyledArea>
        <StyledCardHeader>{t('title')}</StyledCardHeader>
        <StyledGeneralInfoNew>
          <StyledInfoRowNew>
            <div>
              <span className="header">{t('databaseVersion')}</span>
              <span className="value">{universeData?.spec?.yb_software_version}</span>
            </div>
            <div>
              <span className="header">{t('clusterId')}</span>
              <span className="value sameline">
                <Typography variant="body1" fontWeight={'400'} noWrap sx={{ width: '220px' }}>
                  {primaryCluster?.uuid}
                </Typography>
                <CopyIcon
                  onClick={() => {
                    navigator.clipboard.writeText(primaryCluster?.uuid ?? '');
                    toast.success('Copied');
                  }}
                />
              </span>
            </div>
            <div></div>
          </StyledInfoRowNew>
          <StyledInfoRowNew>
            <div>
              <span className="header">{t('infrastructureProvider')}</span>{' '}
              <span className="value">
                <Grid2 container alignItems="center" gap={0.5}>
                  {providerIcon}
                  {providerName ?? ''}
                </Grid2>
              </span>
            </div>
            <div>
              <span className="header">{t('providerConfiguration')}</span>{' '}
              <span className="value">{currentProvider?.name}</span>
            </div>
            <div>
              <span className="header">{t(isK8s ? 'totalPods' : 'totalNodes')}</span>{' '}
              <span className="value">{totalNodesCount}</span>
            </div>
          </StyledInfoRowNew>
          <StyledInfoRowNew>
            <div>
              <span className="header">{t('faultTolerance')}</span>{' '}
              <span className="value">
                {getResilientType(
                  primaryCluster!.placement_spec!,
                  primaryCluster?.replication_factor,
                  t
                )}
              </span>
            </div>
            <div>
              <span className="header">{t('encryption')}</span>{' '}
              <span className="value">
                {encryptionAtTransitEnabled && t('inTransit') + ' '}
                {encryptionAtTransitEnabled && encryptionAtRestEnabled ? ' / ' : ''}
                {encryptionAtRestEnabled && t('atRest') + ' '}
                {!encryptionAtRestEnabled && !encryptionAtTransitEnabled ? '-' : ''}
              </span>
            </div>
            <div>
              <span className="header">{t('dateCreated')}</span>{' '}
              <span className="value">
                {universeData?.info?.creation_date
                  ? ybFormatDate(universeData?.info?.creation_date)
                  : '-'}
              </span>
            </div>
          </StyledInfoRowNew>
        </StyledGeneralInfoNew>
        <Divider />
        <StyledSection>
          <StyledCardHeader>{t('regionsAndPlacement')}</StyledCardHeader>
          <ClusterSubsection title={t('primaryCluster')}>
            <RegionTags
              regions={primaryRegions}
              defaultRegionUuid={primaryCluster?.placement_spec?.cloud_list[0].default_region}
            />
          </ClusterSubsection>
          {readReplicaCluster && (
            <ClusterSubsection title={t('readReplica')}>
              <RegionTags
                regions={readReplicaRegions}
                defaultRegionUuid={readReplicaCluster.placement_spec?.cloud_list[0].default_region}
              />
            </ClusterSubsection>
          )}
          <ViewMoreLink
            href={placementRoute}
            data-testid="general-tab-view-more-placement"
            onClick={navigateToSettingsTab(placementRoute)}
          >
            {t('viewMorePlacement')}
          </ViewMoreLink>
        </StyledSection>
        <Divider />
        <StyledSection>
          <StyledCardHeader>{t('hardware')}</StyledCardHeader>
          <ClusterSubsection title={t('primaryCluster')}>
            <StyledInfoRowNew>
              <div>
                <span className="header">{t('cpuArch')}</span>
                <span className="value">{universeData?.info?.arch}</span>
              </div>
              <LinuxVersion cluster={primaryCluster} />
              <div>
                <span className="header">{t('instanceType')}</span>
                <span className="value">{primaryCluster?.node_spec?.instance_type ?? '-'}</span>
              </div>
            </StyledInfoRowNew>
          </ClusterSubsection>
          {readReplicaCluster && (
            <ClusterSubsection title={t('readReplica')}>
              <StyledInfoRowNew>
                <div>
                  <span className="header">{t('cpuArch')}</span>
                  <span className="value sameline">
                    {universeData?.info?.arch}
                    <YBTag
                      variant="dark"
                      size="small"
                      customSx={{ color: '#4E5F6D', background: '#E9EEF2' }}
                    >
                      {t('sameAsPrimaryCluster')}
                    </YBTag>
                  </span>
                </div>
                <LinuxVersion cluster={readReplicaCluster} />
                <div>
                  <span className="header">{t('instanceType')}</span>
                  <span className="value">
                    {readReplicaCluster.node_spec?.instance_type ?? '-'}
                  </span>
                </div>
              </StyledInfoRowNew>
            </ClusterSubsection>
          )}
          <ViewMoreLink
            href={hardwareRoute}
            data-testid="general-tab-view-more-hardware"
            onClick={navigateToSettingsTab(hardwareRoute)}
          >
            {t('viewMoreHardware')}
          </ViewMoreLink>
        </StyledSection>
      </StyledArea>
    </Box>
  );
};
