import { useQuery } from 'react-query';
import { Box, makeStyles } from '@material-ui/core';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBPanelItem } from '../../panels';
import { CheckPerfAdvisorRegistration } from './CheckPerfAdvisorRegistration';
import {
  PerfAdvisorAPI,
  QUERY_KEY as PERF_ADVISOR_QUERY_KEY
} from '../../../redesign/features/PerfAdvisor/api';
import { AppName } from '../../../redesign/helpers/dtos';
import { isEmptyArray } from '../../../utils/ObjectUtils';
import { ROOT_URL } from '../../../config';
// Side-effect import: registers a CSRF header provider on PA UI's internal
// axios instance so its XHRs pass Play's CSRFFilter. Must be imported by the
// only PA UI entry point in YBA so the setup runs before any PA UI XHR.
import './perfAdvisorHeaders';

interface RegisterYBAToPerfAdvisorProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
}

const useStyles = makeStyles((theme) => ({
  register: {
    cursor: 'pointer'
  }
}));

// PA UI XHRs are proxied through YBA (same-origin) so that YBA can authenticate
// the caller (any auth mode, including SSO) and enforce universe RBAC before
// forwarding to the PA Collector. The base URL corresponds to PAProxyController
// routes in v1.routes and requires `yb.pa.embedded_ui.reverse_proxy.enabled=true`
// on the backend.
//
// The returned URL must be absolute: perf-advisor-ui creates its axios instance
// with baseURL="/api" and prepends it to any non-absolute URL, which would
// otherwise produce a duplicated `/api/api/v1/...` prefix in production (where
// ROOT_URL is "/api/v1").
const buildProxyApiUrl = (customerId: string, paUuid: string) => {
  const absoluteRoot = new URL(ROOT_URL, window.location.href).toString().replace(/\/+$/, '');
  return `${absoluteRoot}/customers/${customerId}/pa_proxy/${paUuid}/api`;
};

export const RegisterYBAToPerfAdvisor = ({
  universeUuid,
  appName,
  timezone
}: RegisterYBAToPerfAdvisorProps) => {
  const helperClasses = useStyles();

  const {
    data: perfAdvisorListData,
    isLoading: isPaListFetchLoading,
    isError: isPaListFetchError,
    isIdle: isPaListFetchIdle
  } = useQuery(PERF_ADVISOR_QUERY_KEY.fetchPerfAdvisorList, () =>
    PerfAdvisorAPI.fetchPerfAdvisorList()
  );

  if (isPaListFetchError) {
    return <YBErrorIndicator customErrorMessage={'Failed to get Performance Advisor details'} />;
  }

  if (isPaListFetchLoading || (isPaListFetchIdle && perfAdvisorListData === undefined)) {
    return <YBLoading />;
  }

  if (isEmptyArray(perfAdvisorListData)) {
    return (
      <YBPanelItem
        body={
          <Box>
            {'Please'}
            <a href={`/config/perfAdvisor/register`} className={helperClasses.register}>
              {' register '}
            </a>
            {'YB Anywhere instance to Performance Advisor Service'}
          </Box>
        }
      />
    );
  }

  const paCollector = perfAdvisorListData?.[0];
  const customerId = paCollector?.customerUUID ?? localStorage.getItem('customerId') ?? '';

  return (
    <CheckPerfAdvisorRegistration
      universeUuid={universeUuid}
      appName={appName}
      timezone={timezone}
      apiUrl={buildProxyApiUrl(customerId, paCollector?.uuid)}
      paUuid={paCollector?.uuid}
    />
  );
};
