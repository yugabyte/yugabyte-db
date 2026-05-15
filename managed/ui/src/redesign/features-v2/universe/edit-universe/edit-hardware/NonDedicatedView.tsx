import { useTranslation } from 'react-i18next';
import { useToggle } from 'react-use';
import { mui } from '@yugabyte-ui-library/core';
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
  const [isClusterEditOpen, setClusterEditOpen] = useToggle(false);
  const [isReadReplicaEditOpen, setReadReplicaEditOpen] = useToggle(false);

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const readReplicaCluster = getClusterByType(universeData!, ClusterSpecClusterType.ASYNC);
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
        title={t('clusterInstance')}
        arch={universeData?.info?.arch}
        nodeSpec={primaryCluster?.node_spec}
        storageSpec={primaryCluster?.node_spec?.storage_spec}
        onEditClicked={() => {
          setClusterEditOpen(true);
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
        visible={isClusterEditOpen}
        mode="cluster"
        onSubmit={() => setClusterEditOpen(false)}
        onHide={() => setClusterEditOpen(false)}
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
