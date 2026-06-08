import {
  getClusterByType,
  hasDedicatedNodes,
  isKubernetesCluster,
  useEditUniverseContext
} from '../EditUniverseUtils';
import { MasterTserverDedicatedView } from '../edit-hardware/MasterTserverDedicatedView';
import { NonDedicatedView } from '../edit-hardware/NonDedicatedView';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { useRuntimeConfigValues } from '../../create-universe/helpers/utils';

export const HardwareTab = () => {
  const { universeData } = useEditUniverseContext();
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const isK8s = isKubernetesCluster(primaryCluster);
  const { useK8CustomResources } = useRuntimeConfigValues(primaryCluster?.provider_spec?.provider);
  const hasDedicatedTo = (isK8s && useK8CustomResources) || hasDedicatedNodes(universeData!);
  return hasDedicatedTo ? <MasterTserverDedicatedView /> : <NonDedicatedView />;
};
