import { useContext } from 'react';
import { GeneralTab } from './settings/GeneralTab';
import { PlacementTab } from './settings/PlacementTab';
import { HardwareTab } from './settings/HardwareTab';
import { EditUniverseContext, EditUniverseTabs } from './EditUniverseContext';
import { SecurityTab } from './settings/SecurityTab';
import { DatabaseTab } from './settings/DatabaseTab';
import { AdvancedTab } from './settings/AdvancedTab';
import { LogsTab } from './settings/logs-tab/LogsTab';
import { TelemetryExportTab } from './settings/telemetry-export-tab/TelemetryExportTab';

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
    case EditUniverseTabs.LOGS:
      return <LogsTab />;
    case EditUniverseTabs.TELEMETRY_EXPORT:
      return <TelemetryExportTab />;
    default:
      return null;
  }
};
