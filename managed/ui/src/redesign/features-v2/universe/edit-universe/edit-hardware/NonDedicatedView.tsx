import { useTranslation } from 'react-i18next';
import { useToggle } from 'react-use';
import { mui } from '@yugabyte-ui-library/core';
import { ClusterType } from '@app/redesign/helpers/dtos';
import {
  useEditUniverseContext,
  countRegionsAzsAndNodes,
  getClusterByType
} from '../EditUniverseUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { EditHardwareConfirmModal } from './EditHardwareConfirmModal';
import { StyledPane } from './Component';
import { InstanceCard } from './InstanceCard';

const { Box, Typography } = mui;

export const NonDedicatedView = () => {
  const { universeData } = useEditUniverseContext();
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.hardware' });
  const [isEditHardwareModalVisible, setEditHardwareModalVisible] = useToggle(false);

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const stats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
      <StyledPane>
        <Box sx={{ display: 'flex', flexDirection: 'row', gap: 2, alignItems: 'center' }}>
          <Typography
            variant="subtitle1"
            fontWeight={500}
            textTransform={'uppercase'}
            color="#6D7C88"
          >
            {t('totalClusterNodes')}
          </Typography>
          <Typography variant="body2" color="#0B1117">
            {stats.totalNodes}
          </Typography>
        </Box>
      </StyledPane>
      <InstanceCard
        title={t('tServerInstance')}
        arch={universeData?.info?.arch}
        nodeSpec={primaryCluster?.node_spec}
        storageSpec={primaryCluster?.node_spec?.storage_spec}
        onEditClicked={() => {
          setEditHardwareModalVisible(true);
        }}
      />
      <EditHardwareConfirmModal
        visible={isEditHardwareModalVisible}
        title={t('tServerInstance')}
        onSubmit={() => {
          setEditHardwareModalVisible(false);
        }}
        onHide={() => {
          setEditHardwareModalVisible(false);
        }}
      />
    </Box>
  );
};
