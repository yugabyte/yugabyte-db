import React from 'react';
import { useLoadHAConfiguration } from '../hooks/useLoadHAConfiguration';
import { withRouter } from 'react-router';
import './StandbyInstanceOverlay.scss';

export const StandbyInstanceOverlay = withRouter<{}>(({ location }) => {
  const { config, error, isNoHAConfigExists, isLoading } = useLoadHAConfiguration(false);
  const currentInstance = config?.instances.find((item) => item.is_local);
  const isAdminRoute = location.pathname.startsWith('/admin/ha');

  if (isAdminRoute || currentInstance?.is_leader || isNoHAConfigExists || error || isLoading) {
    return null;
  }

  return (
    <div className="standby-instance-overlay">
      This is a Standby instance
      <br />
      Only the administration configuration is available
    </div>
  );
});
