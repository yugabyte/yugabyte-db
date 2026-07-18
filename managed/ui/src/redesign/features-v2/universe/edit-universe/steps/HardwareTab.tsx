import { hasDedicatedNodes, useEditUniverseContext } from '../EditUniverseUtils';
import { MasterTserverDedicatedView } from '../edit-hardware/MasterTserverDedicatedView';
import { NonDedicatedView } from '../edit-hardware/NonDedicatedView';

export const HardwareTab = () => {
  const { universeData } = useEditUniverseContext();
  const hasDedicatedTo = hasDedicatedNodes(universeData!);
  return hasDedicatedTo ? <MasterTserverDedicatedView /> : <NonDedicatedView />;
};
