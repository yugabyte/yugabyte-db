import { mui } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { useToggle } from 'react-use';
import {
  countMasterAndTServerNodes,
  getClusterByType,
  useEditUniverseContext
} from '../EditUniverseUtils';
import {
  ClusterSpecClusterType,
  NodeDetailsDedicatedTo
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { StyledPane } from './Component';
import { InstanceCard } from './InstanceCard';
import { EditHardwareConfirmModal } from './EditHardwareConfirmModal';

const { Box, Typography } = mui;

export const MasterTserverDedicatedView = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.hardware' });
  const { universeData } = useEditUniverseContext();
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const readReplicaCluster = getClusterByType(universeData!, ClusterSpecClusterType.ASYNC);
  const masterTserverNodesCount = countMasterAndTServerNodes(
    universeData!,
    primaryCluster!.placement_spec!
  );
  // Each instance card now has its own dedicated edit modal so that the
  // T-Server and Master edits show only their respective sections, per design.
  const [isTServerEditOpen, setTServerEditOpen] = useToggle(false);
  const [isMasterEditOpen, setMasterEditOpen] = useToggle(false);
  const [isReadReplicaEditOpen, setReadReplicaEditOpen] = useToggle(false);
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
        onEditClicked={() => {
          setTServerEditOpen(true);
        }}
      />
      <InstanceCard
        title={t('masterServerInstance')}
        nodeSpec={{
          ...primaryCluster?.node_spec,
          instance_type:
            primaryCluster?.node_spec?.master?.instance_type ??
            primaryCluster?.node_spec?.instance_type
        }}
        storageSpec={
          primaryCluster?.node_spec?.master?.storage_spec ??
          primaryCluster?.node_spec?.storage_spec
        }
        onEditClicked={() => {
          setMasterEditOpen(true);
        }}
      />
      {readReplicaCluster && (
        <InstanceCard
          title={t('rrInstance', { keyPrefix: 'readReplica.addRR' })}
          nodeSpec={readReplicaCluster.node_spec}
          storageSpec={readReplicaCluster.node_spec?.storage_spec}
          onEditClicked={() => {
            setReadReplicaEditOpen(true);
          }}
        />
      )}
      <EditHardwareConfirmModal
        visible={isTServerEditOpen}
        mode="tserver"
        onSubmit={() => setTServerEditOpen(false)}
        onHide={() => setTServerEditOpen(false)}
      />
      <EditHardwareConfirmModal
        visible={isMasterEditOpen}
        mode="master"
        onSubmit={() => setMasterEditOpen(false)}
        onHide={() => setMasterEditOpen(false)}
      />
      {readReplicaCluster && (
        <EditHardwareConfirmModal
          visible={isReadReplicaEditOpen}
          mode="readReplica"
          clusterType={ClusterSpecClusterType.ASYNC}
          onSubmit={() => setReadReplicaEditOpen(false)}
          onHide={() => setReadReplicaEditOpen(false)}
        />
      )}
    </Box>
  );
};
