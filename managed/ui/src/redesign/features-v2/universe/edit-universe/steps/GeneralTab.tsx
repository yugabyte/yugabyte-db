import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { toUpper } from 'lodash';
import { useQuery } from 'react-query';
import { fetchProviderList } from '@app/api/admin';

import { mui, YBMaps, YBSelect, YBTag } from '@yugabyte-ui-library/core';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  extractGeoPartitionsFromUniverse,
  extractRegionsAndNodeDataFromUniverse
} from '../../geo-partition/add/AddGeoPartitionUtils';
import { StyledInfoRow } from '../../create-universe/components/DefaultComponents';
import { ClusterType } from '@app/redesign/helpers/dtos';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { ybFormatDate } from '@app/redesign/helpers/DateUtils';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  getProviderIcon,
  getResilientType,
  useEditUniverseContext
} from '../EditUniverseUtils';

import { getFlagFromRegion } from '../../create-universe/helpers/RegionToFlagUtils';
import { LinuxVersion } from '../components';
import { MapRegionsView } from '../components/MapRegionView';
import { MapGeoPartitionView } from '../components/MapGeoPartitionView';
import { Star } from '@material-ui/icons';
import CopyIcon from '../../../../assets/copy_blue.svg';

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

const StyledGeneralInfo = styled('div')(() => ({
  display: 'flex',
  flexDirection: 'row',
  height: '200px',
  justifyContent: 'space-between'
}));

const StyledYBSelect = styled(YBSelect)(() => ({
  zIndex: 1000,
  margin: '8px',
  width: '200px',
  height: '32px'
}));

enum MapViewMode {
  REGIONS = 'regions',
  GEO_PARTITIONS = 'geo-partitions'
}

export const GeneralTab = () => {
  const { universeData, providerRegions } = useEditUniverseContext();
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.general' });

  const [mapViewMode, setMapViewMode] = useState<MapViewMode>(MapViewMode.REGIONS);
  const r = extractRegionsAndNodeDataFromUniverse(universeData!, providerRegions!);
  const geoParitionsData = extractGeoPartitionsFromUniverse(universeData!, providerRegions!);
  const isGeoPartitionPresent = geoParitionsData.regions.length > 0;

  const encryptionAtTransitEnabled =
    universeData?.spec?.encryption_in_transit_spec?.enable_client_to_node_encrypt ??
    universeData?.spec?.encryption_in_transit_spec?.enable_node_to_node_encrypt;
  const encryptionAtRestEnabled = !!universeData?.spec?.encryption_at_rest_spec?.kms_config_uuid;

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);

  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const providerIcon = getProviderIcon(providerCode);

  const primaryRegionStats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);

  const { data: providers } = useQuery(['providers'], () => fetchProviderList(), {
    select: (data) => data.data
  });

  const currentProvider = providers?.find(
    (provider) => provider.uuid === primaryCluster?.provider_spec.provider
  );

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px', width: '100%' }}>
      <YBMaps
        dataTestId="yb-edit-universe-regions"
        mapHeight={345}
        coordinates={[
          [0, 0],
          [0, 0]
        ]}
        initialBounds={undefined}
        mapContainerProps={{
          scrollWheelZoom: false,
          zoom: 2,
          center: [0, 0]
        }}
      >
        {isGeoPartitionPresent && (
          <StyledYBSelect
            dataTestId="yb-select"
            value={mapViewMode}
            onChange={(e) => setMapViewMode(e.target.value as MapViewMode)}
          >
            <MenuItem value={MapViewMode.REGIONS}>{t('allLocation')}</MenuItem>
            <Divider />
            <MenuItem value={MapViewMode.GEO_PARTITIONS}>{t('geoPartition')}</MenuItem>
          </StyledYBSelect>
        )}
        {mapViewMode === MapViewMode.REGIONS && ((<MapRegionsView regions={r.regions} />) as any)}
        {mapViewMode === MapViewMode.GEO_PARTITIONS && <MapGeoPartitionView />}
      </YBMaps>
      <StyledArea>
        <Typography variant="subtitle2" sx={{ fontSize: '15px' }}>
          {t('title')}
        </Typography>
        <StyledGeneralInfo>
          <StyledInfoRow>
            <div>
              <span className="header">{t('databaseVersion')}</span>
              <span className="value">{universeData?.spec?.yb_software_version}</span>
            </div>
            <div>
              <span className="header">{t('provider')}</span>{' '}
              <span className="value">
                <Grid2 container alignItems="center" gap={0.5}>
                  {providerIcon}
                  {toUpper(providerCode ?? '')}
                </Grid2>
              </span>
            </div>
            <div>
              <span className="header">{t('faultTolerance')}</span>{' '}
              <span className="value">{getResilientType(primaryRegionStats, t)}</span>
            </div>
          </StyledInfoRow>
          <StyledInfoRow>
            <div>
              <span className="header">{t('clusterId')}</span>
              <span className="value sameline">
                <Typography variant="body1" fontWeight={'400'} noWrap sx={{ width: '180px' }}>
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
            <div>
              <span className="header">{t('providerConfig')}</span>{' '}
              <span className="value">{currentProvider?.name}</span>
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
          </StyledInfoRow>
          <StyledInfoRow sx={{ justifyContent: 'flex-end' }}>
            <div></div>
            <div>
              <span className="header">{t('totalNodes')}</span>{' '}
              <span className="value">{primaryRegionStats.totalNodes}</span>
            </div>
            <div>
              <span className="header">{t('dateCreated')}</span>{' '}
              <span className="value">
                {universeData?.info?.creation_date
                  ? ybFormatDate(universeData?.info?.creation_date)
                  : '-'}
              </span>
            </div>
          </StyledInfoRow>
        </StyledGeneralInfo>
        <Divider />
        <div>
          <StyledInfoRow>
            <div>
              <span className="header">{t('primaryCluster')}</span>{' '}
              <span className="value" style={{ display: 'flex', gap: '8px', flexWrap: 'wrap' }}>
                {r.regions.map((region: Region) => {
                  if ((region as any).clusterType !== ClusterType.PRIMARY) return null;
                  return (
                    <YBTag
                      key={region.code}
                      size="medium"
                      endIcon={
                        region.uuid ===
                        primaryCluster?.placement_spec?.cloud_list[0].default_region ? (
                          <Star />
                        ) : undefined
                      }
                    >
                      {getFlagFromRegion(region.code)} {region.name} ({region.code})
                    </YBTag>
                  );
                })}
              </span>
            </div>
          </StyledInfoRow>
        </div>
        <Divider />
        <Box sx={{ display: 'flex', flexDirection: 'row', justifyContent: 'space-between' }}>
          <StyledInfoRow>
            <div>
              <span className="header">{t('cpuArch')}</span>
              <span className="value">{universeData?.info?.arch}</span>
            </div>
          </StyledInfoRow>
          <StyledInfoRow>
            <LinuxVersion />
          </StyledInfoRow>
          <StyledInfoRow>
            <div>
              <span className="header">{t('defaultInstanceType')}</span>
              <span className="value">{primaryCluster?.node_spec?.instance_type ?? '-'}</span>
            </div>
          </StyledInfoRow>
        </Box>
      </StyledArea>
    </Box>
  );
};
