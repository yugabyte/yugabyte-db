import { useContext } from 'react';
import { GeneralTab } from './steps/GeneralTab';
import { PlacementTab } from './steps/PlacementTab';
import { HardwareTab } from './steps/HardwareTab';
import { EditUniverseContext, EditUniverseTabs } from './EditUniverseContext';
import { SecurityTab } from './steps/SecurityTab';
import { DatabaseTab } from './steps/DatabaseTab';
import { AdvancedTab } from './steps/AdvancedTab';

export const SwitchEditUniverseTabs = () => {
  const { activeTab } = useContext(EditUniverseContext);
  switch (activeTab) {
    case EditUniverseTabs.GENERAL:
      return <GeneralTab />;
    case EditUniverseTabs.PLACEMENT:
      return <PlacementTab />;
    case EditUniverseTabs.HARDWARE:
      return <HardwareTab />;
    case EditUniverseTabs.SECURITY:
      return <SecurityTab />;
    case EditUniverseTabs.DATABASE:
      return <DatabaseTab />;
    case EditUniverseTabs.ADVANCED:
      return <AdvancedTab />;
    default:
      return null;
  }
};
