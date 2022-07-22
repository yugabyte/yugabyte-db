import React, { FC, useState } from 'react';
import { YBLoading } from '../../common/indicators';
import { HAErrorPlaceholder } from '../compounds/HAErrorPlaceholder';
import { HAReplicationForm } from './HAReplicationForm';
import { HAReplicationViewContainer } from './HAReplicationViewContainer';

import { useLoadHAConfiguration } from '../hooks/useLoadHAConfiguration';

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
    return <HAErrorPlaceholder error={error} configUUID={config?.uuid!} />;
  }

  if (isNoHAConfigExists) {
    return <HAReplicationForm backToViewMode={backToViewMode} />;
  }

  if (config && schedule) {
    if (isEditingConfig) {
      return (
        <HAReplicationForm config={config} schedule={schedule} backToViewMode={backToViewMode} />
      );
    } else {
      return (
        <HAReplicationViewContainer config={config} schedule={schedule} editConfig={editConfig} />
      );
    }
  }

  return null; // should never get here
};
