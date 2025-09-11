import { useLoadHAConfiguration } from '../hooks/useLoadHAConfiguration';
import { withRouter } from 'react-router';
import { isDefinedNotNull } from '@app/utils/ObjectUtils';
import { AxiosError } from 'axios';
import './StandbyInstanceOverlay.scss';

const allowedURLs = ['/admin/ha', '/logs'];

export const StandbyInstanceOverlay = withRouter<{}>(({ location }) => {
  const { config, error, isNoHAConfigExists, isLoading } = useLoadHAConfiguration({
    loadSchedule: false,
    autoRefresh: false
  });
  const currentInstance = config?.instances.find((item) => item.is_local);
  const isAllowedRoute = allowedURLs.some((str) => location.pathname.startsWith(str));

  if (
    isAllowedRoute ||
    currentInstance?.is_leader ||
    (isDefinedNotNull((error as AxiosError)?.response?.status) &&
      (error as AxiosError)?.response?.status !== 401) ||
    isNoHAConfigExists ||
    isLoading
  ) {
    return null;
  }

  return (
    <>
      <div className="standby-instance-overlay" />
      <div className="standby-instance-text">
        This is a Standby instance.
        <br />
        Only the High Availability configuration is available.
      </div>
    </>
  );
});
