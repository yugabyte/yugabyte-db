import { FC, useState } from 'react';

import { YBLoading } from '../../common/indicators';
import { HAErrorPlaceholder } from '../compounds/HAErrorPlaceholder';
import { HAReplicationViewContainer } from './HAReplicationViewContainer';
import { useLoadHAConfiguration } from '../hooks/useLoadHAConfiguration';
import { HAReplicationFormContainer } from './HAReplicationFormContainer';

export const HAReplication: FC = () => {
  const [isEditingConfig, setEditingConfig] = useState(false);
  const { config, schedule, error, isNoHAConfigExists, isLoading } = useLoadHAConfiguration({
    loadSchedule: true,
    autoRefresh: !isEditingConfig // auto-refresh in view mode only
  });

  const editConfig = () => setEditingConfig(true);
  const backToViewMode = () => setEditingConfig(false);

  if (isLoading) {
    return <YBLoading />;
  }

  if (error) {
    // eslint-disable-next-line @typescript-eslint/no-non-null-asserted-optional-chain
    return <HAErrorPlaceholder error={error} configUUID={config?.uuid!} />;
  }

  if (isNoHAConfigExists) {
    return <HAReplicationFormContainer backToViewMode={backToViewMode} />;
  }

  if (config && schedule) {
    if (isEditingConfig) {
      return (
        <HAReplicationFormContainer
          config={config}
          schedule={schedule}
          backToViewMode={backToViewMode}
        />
      );
    } else {
      return (
        <HAReplicationViewContainer haConfig={config} schedule={schedule} editConfig={editConfig} />
      );
    }
  }

  return null; // should never get here
};
