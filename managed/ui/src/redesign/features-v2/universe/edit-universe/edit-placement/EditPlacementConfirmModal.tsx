import { FC } from 'react';
import { mui, yba, YBTag } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  getResilientType,
  useEditUniverseContext
} from '../EditUniverseUtils';
import { ClusterType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { useGetEditPlacementContext } from './EditPlacementUtils';
import { getFaultToleranceNeeded, getNodeCount } from '../../create-universe/CreateUniverseUtils';
import { getFlagFromRegion } from '../../create-universe/helpers/RegionToFlagUtils';

import pluralize from 'pluralize';
import { keys } from 'lodash';
import { ArrowDownward, ArrowUpward } from '@material-ui/icons';
import NextLineIcon from '@app/redesign/assets/next-line.svg';

interface EditPlacementConfirmModalProps {
  visible: boolean;
  onHide: () => void;
  onSubmit: () => void;
}

const { Box, Typography, styled } = mui;
const { YBModal } = yba;

const StyledRoot = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px'
}));

const StyledPane = styled(Box)(({ theme, border }) => ({
  flex: 1,
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  padding: '20px 20px',
  borderRight: border ? `1px solid ${theme.palette.grey[200]}` : 'none'
}));

const StyledItem = styled(Box)(({ theme }) => ({
  display: 'flex',
  gap: '12px',
  padding: '16px',
  background: '#FBFCFD',
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  alignItems: 'center'
}));

const IncrementIcon = styled(ArrowUpward)(({ theme }) => ({
  color: theme.palette.success[500],
  marginLeft: '8px'
}));

const DecrementIcon = styled(ArrowDownward)(({ theme }) => ({
  color: theme.palette.error[500],
  marginLeft: '8px'
}));

const StyledRegionItem = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  padding: '16px',
  background: '#FBFCFD',
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  fontWeight: 500,
  fontSize: '13px'
}));

const StyledAZItem = styled(StyledItem)(() => ({
  display: 'flex',
  gap: '4px',
  alignItems: 'center',
  border: 'none',
  padding: 0
}));

export const EditPlacementConfirmModal: FC<EditPlacementConfirmModalProps> = ({
  visible,
  onHide,
  onSubmit
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.placement.editPlacementConfirmModal'
  });
  const { universeData } = useEditUniverseContext();
  const [{ nodesAndAvailability, resilience }] = useGetEditPlacementContext();

  if (!visible) return null;
  const primaryCluster = getClusterByType(universeData!, ClusterType.PRIMARY);
  const stats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);
  const resilientType = getResilientType(stats, t).replace('Resilient to ', '');
  const newNodeCount = getNodeCount(nodesAndAvailability!.availabilityZones!);

  const newResilientType = t(`faultToleranceTypes.${resilience?.faultToleranceType}`, {
    count: getFaultToleranceNeeded(resilience!.replicationFactor) - 1
  });

  const currentRegions = universeData?.spec?.clusters
    ?.find((cluster) => cluster.cluster_type === ClusterType.PRIMARY)
    ?.placement_spec?.cloud_list.map((cloud) => cloud?.region_list)
    .flat()
    .sort((a, b) => (a!.name! > b!.name! ? 1 : -1));

  return (
    <YBModal
      open={visible}
      onClose={onHide}
      title={t('title')}
      size="md"
      overrideHeight={'fit-content'}
      dialogContentProps={{ sx: { padding: '24px !important' } }}
      cancelLabel={t('common:cancel')}
      onSubmit={onSubmit}
      submitLabel={t('confirmAndApply')}
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <Typography variant="body2">{t('summary')}</Typography>
        <StyledRoot>
          <StyledPane border="true">
            <Typography variant="body1">{t('current')}</Typography>
            <StyledItem>
              <Typography variant="body2">{t('faultTolerance')}</Typography>
              <YBTag size="medium" variant="dark" color="primary">
                {resilientType}
              </YBTag>
            </StyledItem>
            <StyledItem>
              <Typography variant="body2">{t('totalNodes')}</Typography>
              <YBTag size="medium" variant="dark" color="primary">
                {stats.totalNodes}
              </YBTag>
            </StyledItem>
            {currentRegions?.map((region) => (
              <StyledRegionItem key={region!.name}>
                {getFlagFromRegion(region!.code!)} {region?.name} ({region?.code})
                {region?.az_list?.map((az) => (
                  <StyledAZItem key={az?.name}>
                    <Box sx={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
                      <NextLineIcon />
                      <Typography variant="body2">{az?.name}</Typography>
                    </Box>
                    <YBTag size="medium" variant="dark" color="primary">
                      {az?.num_nodes_in_az}&nbsp;{pluralize(t('node'), az?.num_nodes_in_az)}
                    </YBTag>
                    {az?.leader_preference ? (
                      <YBTag size="medium" variant="dark" color="primary">
                        {t('rank', { rank: az.leader_preference })}
                      </YBTag>
                    ) : (
                      <YBTag size="medium" variant="dark">
                        {t('notPreferred')}
                      </YBTag>
                    )}
                  </StyledAZItem>
                ))}
              </StyledRegionItem>
            ))}
          </StyledPane>
          <StyledPane>
            <Typography variant="body1">{t('new')}</Typography>
            <StyledItem>
              <Typography variant="body2">{t('faultTolerance')}</Typography>
              <YBTag size="medium" variant="dark" color="primary">
                {newResilientType}
              </YBTag>
            </StyledItem>
            <StyledItem>
              <Typography variant="body2">{t('totalNodes')}</Typography>
              <div style={{ display: 'flex', alignItems: 'center' }}>
                <YBTag size="medium" variant="dark" color="primary">
                  {newNodeCount}
                </YBTag>
                {stats.totalNodes < newNodeCount ? (
                  <IncrementIcon />
                ) : stats.totalNodes > newNodeCount ? (
                  <DecrementIcon />
                ) : null}
              </div>
            </StyledItem>
            {keys(nodesAndAvailability!.availabilityZones!).map((regionKey) => {
              const region = resilience?.regions.find((r) => r.code === regionKey);
              const az_list = nodesAndAvailability!.availabilityZones![regionKey];
              return (
                <StyledRegionItem key={regionKey}>
                  {getFlagFromRegion(region!.code!)} {region?.name} ({region?.code})
                  {az_list?.map((az) => (
                    <StyledAZItem key={az?.name}>
                      <Box sx={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
                        <NextLineIcon />
                        <Typography variant="body2">{az?.name}</Typography>
                      </Box>
                      <YBTag size="medium" variant="dark" color="primary">
                        {az?.nodeCount}&nbsp;{pluralize(t('node'), az?.nodeCount)}
                      </YBTag>
                      {az?.preffered > -1 ? (
                        <YBTag size="medium" variant="dark" color="primary">
                          {t('rank', { rank: az.preffered + 1 })}
                        </YBTag>
                      ) : (
                        <YBTag size="medium" variant="dark">
                          {t('notPreferred')}
                        </YBTag>
                      )}
                    </StyledAZItem>
                  ))}
                </StyledRegionItem>
              );
            })}
          </StyledPane>
        </StyledRoot>
      </Box>
    </YBModal>
  );
};
