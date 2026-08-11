import { mui } from '@yugabyte-ui-library/core';
import { StyledPane } from './Component';
import { useTranslation } from 'react-i18next';
import {
  countMasterAndTServerNodes,
  getClusterByType,
  useEditUniverseContext
} from '../EditUniverseUtils';
import {
  ClusterSpecClusterType,
  NodeDetailsDedicatedTo
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { InstanceCard } from './InstanceCard';
import { EditHardwareConfirmModal } from './EditHardwareConfirmModal';
import { useToggle } from 'react-use';

const { Box, Typography } = mui;

export const MasterTserverDedicatedView = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.hardware' });
  const { universeData } = useEditUniverseContext();
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const masterTserverNodesCount = countMasterAndTServerNodes(
    universeData!,
    primaryCluster!.placement_spec!
  );
  const [isEditHardwareModalVisible, setEditHardwareModalVisible] = useToggle(false);
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
            {t('totalNodesTServerMaster', {
              total_nodes:
                masterTserverNodesCount[NodeDetailsDedicatedTo.TSERVER]! +
                masterTserverNodesCount[NodeDetailsDedicatedTo.MASTER]!,
              tservers: masterTserverNodesCount[NodeDetailsDedicatedTo.TSERVER] ?? 0,
              masters: masterTserverNodesCount[NodeDetailsDedicatedTo.MASTER] ?? 0,
              keyPrefix: 'editUniverse.placement'
            })}
          </Typography>
        </Box>
      </StyledPane>
      <InstanceCard
        title={t('tServerInstance')}
        arch={universeData?.info?.arch}
        nodeSpec={primaryCluster?.node_spec}
        storageSpec={primaryCluster?.node_spec?.storage_spec}
        onEditClicked={() => {}}
      />
      <InstanceCard
        title={t('masterServerInstance')}
        nodeSpec={primaryCluster?.node_spec}
        storageSpec={primaryCluster?.node_spec?.storage_spec}
        onEditClicked={() => {
          setEditHardwareModalVisible(true);
        }}
      />
      <EditHardwareConfirmModal
        visible={isEditHardwareModalVisible}
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
