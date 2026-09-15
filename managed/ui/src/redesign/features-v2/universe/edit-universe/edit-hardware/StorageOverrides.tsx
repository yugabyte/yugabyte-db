import { FC, useMemo } from 'react';
import { mui, YBButton, YBTag } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { useToggle } from 'react-use';
import AddIcon from '@app/redesign/assets/add.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import StarIcon from '@app/redesign/assets/in-use-star.svg';
import { ClusterSpec, ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { useEditUniverseContext, useIsUniverseReady, withUniverseResource } from '../EditUniverseUtils';
import { getFlagFromRegion } from '../../create-universe/helpers/RegionToFlagUtils';
import {
  buildStorageOverrideModalProps,
  StorageOverridesModal
} from './StorageOverridesModal';
import {
  getAzListFromCluster,
  getStorageOverrideZoneChips,
  hasStorageOverrides
} from './storageOverridesUtils';

const { Box, Typography, styled } = mui;

const Root = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  background: theme.palette.common.white,
}));

const Header = styled(Box)({
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  padding: '10px 24px',
  background: '#fff'
});

const Content = styled(Box)({
  padding: '8px 24px 24px 24px',
  background: '#fff'
});

const EmptyState = styled(Box)(({ theme }) => ({
  borderRadius: '8px',
  display: 'flex',
  flexDirection: 'column',
  justifyContent: 'center',
  alignItems: 'center',
  gap: '16px',
  alignSelf: 'stretch',
  height: '168px',
  border: `1px dashed ${theme.palette.primary[300]}`,
  background: theme.palette.primary[100]
}));

const OverrideCard = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '6px',
  borderRadius: '8px',
  background: theme.palette.common.white
}));

const OverrideCardHeader = styled(Box)({
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  gap: '16px'
});

const ZoneTagList = styled(Box)({
  display: 'flex',
  flexWrap: 'wrap',
  alignItems: 'center',
  gap: '8px'
});

interface StorageOverridesProps {
  cluster: ClusterSpec;
  hasReadReplica: boolean;
}

export const StorageOverrides: FC<StorageOverridesProps> = ({ cluster, hasReadReplica = false }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.hardware.storageOverrides'
  });
  const isUniverseReady = useIsUniverseReady();
  const { universeData } = useEditUniverseContext();
  const universeUUID = universeData?.info?.universe_uuid;
  const [isModalOpen, setModalOpen] = useToggle(false);

  const hasOverrides = hasStorageOverrides(cluster);
  const azList = useMemo(() => getAzListFromCluster(cluster), [cluster]);
  const modalProps = useMemo(
    () => buildStorageOverrideModalProps(cluster),
    [cluster]
  );

  const zoneChips = useMemo(
    () => (hasOverrides ? getStorageOverrideZoneChips(cluster) : []),
    [hasOverrides, cluster]
  );

  const openModal = () => setModalOpen(true);
  const closeModal = () => setModalOpen(false);

  const title = !hasReadReplica ? t('title') : cluster.cluster_type === ClusterSpecClusterType.PRIMARY ? t('primaryClusterTitle') : t('asyncClusterTitle');

  return (
    <Root>
      <Header>
        <Typography variant="h5">{title}</Typography>
        {
          hasOverrides && (<RbacValidator
            accessRequiredOn={withUniverseResource(
              ApiPermissionMap.EDIT_V2_UNIVERSE_PLACEMENT,
              universeUUID
            )}
            isControl
          >
            <YBButton
              dataTestId="edit-storage-override"
              variant="ghost"
              startIcon={<EditIcon />}
              onClick={openModal}
              disabled={!isUniverseReady || azList.length === 0}
            >
              {t('edit', { keyPrefix: 'common' })}
            </YBButton>
          </RbacValidator>)
        }
      </Header>
      <Content>
        {hasOverrides ? (
          <OverrideCard>
            <OverrideCardHeader>
              <Typography variant="subtitle1" color="textSecondary">
                {t('zonesWithStorageOverrides')}
              </Typography>

            </OverrideCardHeader>
            <ZoneTagList>
              {zoneChips.map((chip) => (
                <YBTag key={chip.azUuid} size="medium" variant='light' endIcon={chip.isPreferred ? <StarIcon /> : undefined}>
                  {getFlagFromRegion(chip.regionCode)} {chip.regionName} ({chip.azName})
                </YBTag>
              ))}
            </ZoneTagList>
          </OverrideCard>
        ) : (
          <EmptyState>
            <Typography variant="body2" color="#4E5F6D">
              {t('emptyStateText')}
            </Typography>
            <RbacValidator
              accessRequiredOn={withUniverseResource(
                ApiPermissionMap.EDIT_V2_UNIVERSE_PLACEMENT,
                universeUUID
              )}
              isControl
            >
              <YBButton
                startIcon={<AddIcon />}
                dataTestId="add-storage-override"
                variant="secondary"
                color="primary"
                onClick={openModal}
                disabled={!isUniverseReady || azList.length === 0}
              >
                {t('addStorageOverride')}
              </YBButton>
            </RbacValidator>
          </EmptyState>
        )}
      </Content>
      {isModalOpen && (
        <StorageOverridesModal
          open={isModalOpen}
          onClose={closeModal}
          azList={modalProps.azList}
          initialRows={modalProps.initialRows}
          cluster={cluster}
        />
      )}
    </Root>
  );
};
