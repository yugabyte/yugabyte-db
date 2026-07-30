import { Trans, useTranslation } from 'react-i18next';
import { useMemo, useState } from 'react';
import { browserHistory } from 'react-router';
import { Box } from '@material-ui/core';

import {
  PerfAdvisorEntry,
  MetricsAnalysisEntry,
  buildQueryDrilldownUrl,
  isNonEmptyString,
  URL_TAB_PATH,
  type QueryPageParams
} from '@yugabytedb/perf-advisor-ui';

import { AppName } from '@app/redesign/helpers/dtos';
import { YBPanelItem } from '../../panels';

import { usePerfAdvisorRuntimeConfigs } from './usePerfAdvisorRuntimeConfigs';
import { getQueryIdFromUrl, getTroubleshootUuidFromUrl } from './perfAdvisorUrlHelpers';

interface PerfAdvisorOverviewDashboardProps {
  universeUuid: string;
  timezone: string;
  apiUrl: string;
  registrationStatus: boolean;
  // When true, the dashboard renders the standalone anomalies view.
  showStandaloneAnomalies?: boolean;
}

export const PerfAdvisorOverviewDashboard = ({
  universeUuid,
  timezone,
  apiUrl,
  registrationStatus,
  showStandaloneAnomalies = false
}: PerfAdvisorOverviewDashboardProps) => {
  const { t } = useTranslation();
  const runtimeConfigs = usePerfAdvisorRuntimeConfigs();

  const [troubleshootUuid, setTroubleshootUuid] = useState<string | null>(
    getTroubleshootUuidFromUrl
  );
  const [queryId, setQueryId] = useState<string | null>(getQueryIdFromUrl);

  // In-app (SPA) navigation without a full page reload.
  const onNavigateToUrl = (url: string) => {
    browserHistory.push(url);
  };

  const onSelectedIssue = (selectedIssueId: string | null) => {
    setTroubleshootUuid(selectedIssueId);
    setQueryId(null);
  };

  const onSelectedQuery = (selectedQueryId: string | null, params?: QueryPageParams) => {
    if (!selectedQueryId) {
      setQueryId(null);
      const urlParams = new URLSearchParams(window.location.search);
      urlParams.delete('queryId');
      const cleanedPath = window.location.pathname.replace(/\/queries\/[^/]+$/, '');
      const qs = urlParams.toString();
      browserHistory.replace(qs ? `${cleanedPath}?${qs}` : cleanedPath);
      return;
    }

    const currentPath = window.location.pathname;
    const isInAnomaly = new RegExp(`/${URL_TAB_PATH.ANOMALIES}/[^/]+`).test(currentPath);
    const basePath = currentPath.replace(/\/queries\/[^/]+$/, '');
    const pathSegments = isInAnomaly ? ['queries', selectedQueryId] : [];

    const url = buildQueryDrilldownUrl({
      basePath,
      baseSearch: window.location.search,
      pathSegments,
      searchParams: {
        queryId: selectedQueryId,
        universeId: params?.universeId ?? undefined,
        type: params?.type ?? undefined,
        dbId: params?.dbId ?? undefined
      }
    });

    // Query click opens the drilldown in a new tab (parity with YBM / WEB).
    window.open(url, '_blank', 'noopener,noreferrer');
  };

  const shouldRenderDrilldown = isNonEmptyString(queryId!) || isNonEmptyString(troubleshootUuid!);

  // Memoize each view to avoid unnecessary re-creation when inputs are unchanged.
  const memoizedDrilldownView = useMemo(() => {
    if (!shouldRenderDrilldown) return null;
    return (
      <MetricsAnalysisEntry
        universeUuid={universeUuid}
        troubleshootUuid={troubleshootUuid}
        appName={AppName.YBA}
        apiUrl={apiUrl}
        runtimeConfigs={runtimeConfigs}
        onSelectedIssue={onSelectedIssue}
        onSelectedQuery={onSelectedQuery}
        onNavigateToUrl={onNavigateToUrl}
      />
    );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shouldRenderDrilldown, universeUuid, troubleshootUuid, apiUrl, runtimeConfigs]);

  const memoizedPerfAdvisorEntry = useMemo(() => {
    return (
      <PerfAdvisorEntry
        universeUuid={universeUuid}
        appName={AppName.YBA}
        apiUrl={apiUrl}
        runtimeConfigs={runtimeConfigs}
        onSelectedIssue={onSelectedIssue}
        onSelectedQuery={onSelectedQuery}
        onNavigateToUrl={onNavigateToUrl}
        showStandaloneAnomalies={showStandaloneAnomalies}
      />
    );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [universeUuid, apiUrl, showStandaloneAnomalies, runtimeConfigs]);

  if (!registrationStatus) {
    return (
      <YBPanelItem
        body={
          <Box>
            <span>
              <Trans
                i18nKey={t('clusterDetail.troubleshoot.enablePerfAdvisorMsg')}
                components={{ bold: <b /> }}
              />
            </span>
          </Box>
        }
      />
    );
  }

  return shouldRenderDrilldown ? <>{memoizedDrilldownView}</> : <>{memoizedPerfAdvisorEntry}</>;
};
