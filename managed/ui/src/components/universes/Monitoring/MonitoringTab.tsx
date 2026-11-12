import { FC, ReactNode, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { withRouter, WithRouterProps } from 'react-router';
import { mui, YBTab, YBTabs, YBTag } from '@yugabyte-ui-library/core';
import { AppName } from '@app/redesign/helpers/dtos';
import { PaRegistrationMode } from '@app/redesign/features/PerfAdvisor/api';
import { YBLoading } from '../../common/indicators';
import { RegisterYBAToPerfAdvisor } from '../AttachUniverseToPerfAdvisor/RegisterYBAToPerfAdvisor';
import { usePerfAdvisorRuntimeConfigs } from '../AttachUniverseToPerfAdvisor/usePerfAdvisorRuntimeConfigs';
import { MetricsContainerWidthContext } from '../../metrics/MetricsContainerWidthContext';
import { MonitoringMovedTip } from './MonitoringMovedTip';
import {
  MetricsSubTab,
  MonitoringItem,
  PERF_ADVISOR_ITEMS,
  getMonitoringItemPath,
  getMonitoringNav,
  getMonitoringRedirect,
  isMonitoringNavShown
} from './monitoringUrlHelpers';
import MetricsIcon from '@app/redesign/assets/monitoring/stats.svg';
import QueriesIcon from '@app/redesign/assets/monitoring/queries-nav.svg';
import ClusterLoadIcon from '@app/redesign/assets/monitoring/cluster-load-nav.svg';
import DatabasesIcon from '@app/redesign/assets/monitoring/databases-nav.svg';

const { Box } = mui;

const ITEM_ICONS: Record<MonitoringItem, ReactNode> = {
  [MonitoringItem.METRICS]: <MetricsIcon />,
  [MonitoringItem.QUERIES]: <QueriesIcon />,
  [MonitoringItem.CLUSTER_LOAD]: <ClusterLoadIcon />,
  [MonitoringItem.DATABASES]: <DatabasesIcon />
};

const NAV_TAB_WIDTH = 200;

// MetricsPanel draws two charts per row, each `(containerWidth - 83px) / 2` wide plus 1px
// padding and 7.5px margins each side. In the chart card (16px padding and a 1px border each
// side), the card's inner width plus 47px fills the row and leaves 2px for rounding.
const METRICS_CARD_INSET = 34;
const METRICS_CONTAINER_EXTRA = 47;

// Collapsing the sidebar is remembered per browser, as in YBM.
const NAV_COLLAPSED_KEY = '__yb_monitoring_nav_collapsed__';

const readNavCollapsed = () => {
  try {
    return localStorage.getItem(NAV_COLLAPSED_KEY) === 'true';
  } catch {
    return false;
  }
};

const writeNavCollapsed = (collapsed: boolean) => {
  try {
    localStorage.setItem(NAV_COLLAPSED_KEY, String(collapsed));
  } catch {
    // Without storage the sidebar just opens expanded next time.
  }
};

interface MonitoringTabProps {
  universeUuid: string;
  timezone: string;
  showClassicMetrics: boolean;
  showQueries: boolean;
  showClusterLoad: boolean;
  showDatabases: boolean;
  registrationMode?: PaRegistrationMode;
  isPerfAdvisorPending: boolean;
  classicMetrics: ReactNode;
  queries: ReactNode;
}

const MonitoringTabComponent: FC<MonitoringTabProps & WithRouterProps> = ({
  universeUuid,
  timezone,
  showClassicMetrics,
  showQueries,
  showClusterLoad,
  showDatabases,
  registrationMode,
  isPerfAdvisorPending,
  classicMetrics,
  queries,
  location,
  router
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'clusterDetail.monitoring' });

  const [remountKey, setRemountKey] = useState(0);
  const [isNavCollapsed, setNavCollapsed] = useState(readNavCollapsed);

  const handleNavCollapsedChange = (collapsed: boolean) => {
    setNavCollapsed(collapsed);
    writeNavCollapsed(collapsed);
  };

  const contentRef = useRef<HTMLDivElement>(null);
  const [contentWidth, setContentWidth] = useState<number | null>(null);
  useEffect(() => {
    const content = contentRef.current;
    if (!content || typeof ResizeObserver === 'undefined') {
      return undefined;
    }
    const observer = new ResizeObserver(([entry]) =>
      setContentWidth(Math.round(entry.contentRect.width))
    );
    observer.observe(content);
    return () => observer.disconnect();
  }, []);
  const metricsContainerWidth = contentWidth
    ? contentWidth - METRICS_CARD_INSET + METRICS_CONTAINER_EXTRA
    : null;

  const { showDatabasesView } = usePerfAdvisorRuntimeConfigs();

  const settledPerfAdvisor = useRef<{
    universeUuid: string;
    showClusterLoad: boolean;
    showDatabases: boolean;
    registrationMode?: PaRegistrationMode;
  } | null>(null);
  if (!isPerfAdvisorPending) {
    settledPerfAdvisor.current = { universeUuid, showClusterLoad, showDatabases, registrationMode };
  }
  const settled =
    settledPerfAdvisor.current?.universeUuid === universeUuid ? settledPerfAdvisor.current : null;
  const isPaPending = isPerfAdvisorPending && !settled;
  const useSettled = isPerfAdvisorPending && !!settled;
  const showPaClusterLoad = useSettled ? settled.showClusterLoad : showClusterLoad;
  const showPaDatabases =
    (useSettled ? settled.showDatabases : showDatabases) && !!showDatabasesView;
  const paRegistrationMode = useSettled ? settled.registrationMode : registrationMode;

  const metricsSubTabs: MetricsSubTab[] = [
    ...(showClassicMetrics ? [MetricsSubTab.CLASSIC] : []),
    ...(showPaClusterLoad ? [MetricsSubTab.ADVANCED] : [])
  ];
  const items: MonitoringItem[] = [
    ...(metricsSubTabs.length ? [MonitoringItem.METRICS] : []),
    ...(showQueries ? [MonitoringItem.QUERIES] : []),
    ...(showPaClusterLoad ? [MonitoringItem.CLUSTER_LOAD] : []),
    ...(showPaDatabases ? [MonitoringItem.DATABASES] : [])
  ];
  const getItemPath = (target: MonitoringItem) =>
    getMonitoringItemPath(universeUuid, target, metricsSubTabs[0]);

  const pathname = location?.pathname;
  const nav = getMonitoringNav(pathname);
  const { item, metricsSubTab } = nav;
  const isShown = isMonitoringNavShown(nav, items, metricsSubTabs);

  // The pane stays mounted while the universe tab fades out; other tabs' paths return null.
  const redirectPath = getMonitoringRedirect({
    universeUuid,
    pathname,
    search: location?.search ?? '',
    items,
    metricsSubTabs,
    isPerfAdvisorPending: isPaPending
  });
  useEffect(() => {
    if (redirectPath) {
      router.replace(redirectPath);
    }
  }, [redirectPath, router]);

  const handleItemChange = (_event: unknown, target: MonitoringItem) => {
    router.push(getItemPath(target));
  };

  const handleItemClick = (target: MonitoringItem) => {
    if (target !== item) {
      return;
    }
    setRemountKey((key) => key + 1);
    const targetPath =
      target === MonitoringItem.METRICS && metricsSubTab !== null
        ? getMonitoringItemPath(universeUuid, target, metricsSubTab)
        : getItemPath(target);
    if (`${window.location.pathname}${window.location.search}` !== targetPath) {
      router.push(targetPath);
    }
  };

  const handleMetricsSubTabChange = (_event: unknown, target: MetricsSubTab) => {
    router.push(getMonitoringItemPath(universeUuid, MonitoringItem.METRICS, target));
  };

  const newTag = (
    <YBTag variant="light" size="small" color="gradient" customSx={{ marginLeft: '8px' }}>
      {t('new')}
    </YBTag>
  );

  const perfAdvisorView = (
    <RegisterYBAToPerfAdvisor
      universeUuid={universeUuid}
      timezone={timezone}
      appName={AppName.YBA}
      registrationMode={paRegistrationMode}
    />
  );

  const renderView = () => {
    if (!isShown) {
      return <YBLoading />;
    }
    if (item === MonitoringItem.METRICS) {
      return metricsSubTab === MetricsSubTab.CLASSIC ? (
        <MetricsContainerWidthContext.Provider value={metricsContainerWidth}>
          {classicMetrics}
        </MetricsContainerWidthContext.Provider>
      ) : (
        perfAdvisorView
      );
    }
    if (item === MonitoringItem.QUERIES) {
      return queries;
    }
    return perfAdvisorView;
  };

  return (
    <Box
      sx={{
        display: 'flex',
        width: '100%',
        minHeight: 0,
        '& .text-center.loading-icon-container': {
          position: 'static',
          margin: '40px auto'
        }
      }}
      data-testid="MonitoringTab"
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', flexShrink: 0 }}>
        <YBTabs
          orientation="vertical"
          variant="secondary"
          tabWidth={NAV_TAB_WIDTH}
          collapsible
          collapsed={isNavCollapsed}
          onCollapsedChange={handleNavCollapsedChange}
          value={isShown ? item : false}
          onChange={handleItemChange}
          sx={isNavCollapsed ? undefined : { width: NAV_TAB_WIDTH + 32 }}
        >
          {items.map((target) => (
            <YBTab
              key={target}
              value={target}
              label={t(target)}
              shortLabel={t(`${target}Short`)}
              startIcon={ITEM_ICONS[target]}
              endIcon={
                !isNavCollapsed &&
                (PERF_ADVISOR_ITEMS.includes(target) ||
                  (target === MonitoringItem.METRICS && showPaClusterLoad))
                  ? newTag
                  : undefined
              }
              onClick={() => handleItemClick(target)}
              data-testid={`MonitoringTab-${target}`}
            />
          ))}
        </YBTabs>
        {!isNavCollapsed && (
          <Box sx={{ marginLeft: '16px' }}>
            <MonitoringMovedTip
              showClusterLoadRows={showPaClusterLoad}
              showDatabasesRow={showPaDatabases}
            />
          </Box>
        )}
      </Box>

      <Box ref={contentRef} sx={{ flex: 1, minWidth: 0, ml: 2, pt: 2 }}>
        {isShown && item === MonitoringItem.METRICS && metricsSubTabs.length > 1 && (
          <Box mb={2}>
            <YBTabs value={metricsSubTab} onChange={handleMetricsSubTabChange} sx={{ pl: 0 }}>
              <YBTab
                value={MetricsSubTab.CLASSIC}
                label={t('classicMetrics')}
                data-testid="MonitoringTab-metrics-classic"
              />
              <YBTab
                value={MetricsSubTab.ADVANCED}
                label={t('advancedMetrics')}
                endIcon={newTag}
                data-testid="MonitoringTab-metrics-advanced"
              />
            </YBTabs>
          </Box>
        )}

        <Box
          key={`${item}/${metricsSubTab}/${remountKey}`}
          sx={item === MonitoringItem.DATABASES ? { pt: 1.5 } : undefined}
        >
          {renderView()}
        </Box>
      </Box>
    </Box>
  );
};

export const MonitoringTab = withRouter(MonitoringTabComponent);
