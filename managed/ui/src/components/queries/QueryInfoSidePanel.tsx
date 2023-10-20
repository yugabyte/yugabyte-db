/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from 'react';
import clsx from 'clsx';
import { Bar, BarChart, CartesianGrid, Tooltip, XAxis, YAxis } from 'recharts';
import { Box, makeStyles, Tab, Typography, useTheme } from '@material-ui/core';
import { TabContext, TabList, TabPanel } from '@material-ui/lab';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';

import { CodeBlock } from './CodeBlock';
import {
  DURATION_FIELDS,
  DURATION_FIELD_DECIMALS,
  QueryType,
  TIMESTAMP_FIELDS
} from './helpers/constants';
import {
  YcqlLiveQueryPrimativeFields,
  YsqlLiveQuery,
  YsqlLiveQueryPrimativeFields,
  YsqlSlowQuery,
  YsqlSlowQueryPrimativeFields
} from './helpers/types';
import { adaptHistogramData } from './helpers/utils';
import { parseFloatIfDefined } from '../xcluster/ReplicationUtils';
import { QueryApi } from '../../redesign/helpers/constants';
import { formatDatetime, YBTimeFormats } from '../../redesign/helpers/DateUtils';

interface QueryInfoSidePanelBaseProps {
  visible: boolean;
  onHide: () => void;
}
interface SlowQueryInfoSidePanelProps extends QueryInfoSidePanelBaseProps {
  queryApi: typeof QueryApi.YSQL;
  queryType: typeof QueryType.SLOW;
  queryData: YsqlSlowQuery;
}
interface LiveQueryInfoSidePanelProps extends QueryInfoSidePanelBaseProps {
  queryApi: QueryApi;
  queryType: typeof QueryType.LIVE;
  queryData: YsqlLiveQuery;
}
type QueryInfoSidePanelProps = SlowQueryInfoSidePanelProps | LiveQueryInfoSidePanelProps;

/**
 * This constant stores the order of the `YSQLSlowQuery` fields to display to the user.
 */
const YSQL_SLOW_QUERY_DETAIL_FIELDS: YsqlSlowQueryPrimativeFields[] = [
  'calls',
  'rows',
  'datname',
  'rolname',
  'total_time',
  'max_time',
  'min_time',
  'mean_time',
  'stddev_time',
  'P25',
  'P50',
  'P90',
  'P95',
  'P99'
];

const YSQL_LIVE_QUERY_DETAIL_FIELDS: YsqlLiveQueryPrimativeFields[] = [
  'dbName',
  'queryStartTime',
  'elapsedMillis',
  'nodeName',
  'privateIp',
  'clientHost',
  'clientPort',
  'appName',
  'sessionStatus'
];

const YCQL_LIVE_QUERY_DETAIL_FIELDS: YcqlLiveQueryPrimativeFields[] = [
  'clientHost',
  'clientPort',
  'elapsedMillis',
  'keyspace',
  'nodeName',
  'privateIp',
  'type'
];

const useStyles = makeStyles((theme) => ({
  queryMetricsContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3)
  },
  queryInfoTabList: {
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  },
  queryInfoTabContainer: {
    marginTop: theme.spacing(2),
    padding: theme.spacing(2),

    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: '8px'
  },
  header: {
    display: 'flex',
    alignItems: 'center',

    padding: `${theme.spacing(2)}px ${theme.spacing(3)}px`,

    background: theme.palette.common.white,
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  headerIcon: {
    marginLeft: 'auto',

    fontSize: '16px'
  },
  content: {
    padding: `${theme.spacing(2)}px ${theme.spacing(3)}px`
  },
  queryMetricRow: {
    display: 'flex'
  },
  queryMetricLabel: {
    width: '145px',
    minWidth: '145px'
  },
  totalCountMetric: {
    fontSize: '15px'
  },
  latencyGraph: {
    marginTop: theme.spacing(2)
  }
}));

const QueryInfoTab = {
  RESPONSE_TIME: 'responseTime',
  DETAILS: 'details'
} as const;
type QueryInfoTab = typeof QueryInfoTab[keyof typeof QueryInfoTab];

const DEFAULT_TAB = QueryInfoTab.RESPONSE_TIME;
const TRANSLATION_KEY_PREFIX = 'clusterDetail.performance.queryDetailsPanel';
const NO_DATA_PLACEHOLDER_TEXT = '-';

export const QueryInfoSidePanel = ({
  queryApi,
  queryType,
  queryData,
  visible,
  onHide
}: QueryInfoSidePanelProps) => {
  const [currentTab, setCurrentTab] = useState<QueryInfoTab>(DEFAULT_TAB);
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const { t, i18n } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });
  const classes = useStyles();
  const theme = useTheme();
  const handleTabChange = (_event: React.ChangeEvent<{}>, newTab: QueryInfoTab) => {
    setCurrentTab(newTab);
  };
  const translateWithFallback = (key: string, fallback: string) =>
    i18n.exists(`${TRANSLATION_KEY_PREFIX}.${key}`) ? t(key) : fallback;

  const formatQueryData = (
    ysqlSlowQueryKey:
      | YsqlLiveQueryPrimativeFields
      | YcqlLiveQueryPrimativeFields
      | YsqlSlowQueryPrimativeFields
  ): string => {
    const queryDataEntry = queryData?.[ysqlSlowQueryKey];

    // The following formatters expect only string or finite number types.
    // This guard is used to catch undefined values or unhandled types.
    if (
      !(
        typeof queryDataEntry === 'string' ||
        queryDataEntry instanceof String ||
        Number.isFinite(queryDataEntry)
      )
    ) {
      return NO_DATA_PLACEHOLDER_TEXT;
    }

    if ((DURATION_FIELDS as readonly string[]).includes(ysqlSlowQueryKey)) {
      const duration = parseFloatIfDefined(queryDataEntry);
      return duration === undefined || isNaN(duration)
        ? NO_DATA_PLACEHOLDER_TEXT
        : `${duration?.toFixed(DURATION_FIELD_DECIMALS)} ms`;
    }

    if ((TIMESTAMP_FIELDS as readonly string[]).includes(ysqlSlowQueryKey)) {
      return formatDatetime(
        queryDataEntry,
        YBTimeFormats.YB_DEFAULT_TIMESTAMP,
        currentUserTimezone
      );
    }

    return queryDataEntry.toString();
  };

  /** `yb_latency_histogram` is not returned from queries on older YBDB versions */
  const latencyBarGraphData =
    queryType === QueryType.SLOW && queryData?.yb_latency_histogram
      ? adaptHistogramData(queryData.yb_latency_histogram)
      : [];
  const queryMetricFields =
    queryType === QueryType.SLOW
      ? YSQL_SLOW_QUERY_DETAIL_FIELDS
      : queryApi === QueryApi.YSQL
      ? YSQL_LIVE_QUERY_DETAIL_FIELDS
      : YCQL_LIVE_QUERY_DETAIL_FIELDS;
  const islatencyPercentilesSupported = queryType === QueryType.SLOW;

  const queryDetails = queryData
    ? queryMetricFields.map((queryKey) => {
        return (
          <div className={classes.queryMetricRow} key={queryKey}>
            <Typography className={classes.queryMetricLabel} variant="body2" color="textSecondary">
              {translateWithFallback(`field.${queryKey}`, queryKey)}
            </Typography>
            <Typography variant="body2">{formatQueryData(queryKey)}</Typography>
          </div>
        );
      })
    : null;

  return (
    <div className={`side-panel ${!visible ? 'panel-hidden' : ''}`}>
      <div className={classes.header}>
        <Typography variant="h4">{t('title')}</Typography>
        <i className={clsx('fa fa-close', classes.headerIcon)} onClick={onHide} />
      </div>
      {queryData && (
        <div className={classes.content}>
          <CodeBlock code={queryData.query} />
          {islatencyPercentilesSupported ? (
            <TabContext value={currentTab}>
              <TabList
                classes={{ root: classes.queryInfoTabList }}
                TabIndicatorProps={{
                  style: { backgroundColor: theme.palette.ybacolors.ybOrangeFocus }
                }}
                onChange={handleTabChange}
                aria-label={t('aria.queryInfoPanelTabs')}
              >
                <Tab label={t('label.responseTimePercentile')} value={QueryInfoTab.RESPONSE_TIME} />
                <Tab label={t('label.details')} value={QueryInfoTab.DETAILS} />
              </TabList>
              <div className={classes.queryInfoTabContainer}>
                <TabPanel value={QueryInfoTab.RESPONSE_TIME}>
                  <div>
                    <Box display="flex" flexDirection="column" gridGap="24px" marginBottom={2}>
                      <Box>
                        <Typography variant="body1" className={classes.totalCountMetric}>
                          {t('field.calls')}
                        </Typography>
                        <Typography variant="body2" className={classes.totalCountMetric}>
                          {formatQueryData('calls')}
                        </Typography>
                      </Box>
                      <Box display="flex" gridGap="16px">
                        {(['max_time', 'min_time', 'mean_time'] as const).map(
                          (ysqlSlowQueryKey) => (
                            <div key={ysqlSlowQueryKey}>
                              <Typography variant="body1">
                                {translateWithFallback(
                                  `field.${ysqlSlowQueryKey}`,
                                  ysqlSlowQueryKey
                                )}
                              </Typography>
                              <Typography variant="body2">
                                {formatQueryData(ysqlSlowQueryKey)}
                              </Typography>
                            </div>
                          )
                        )}
                      </Box>
                      <Box display="flex" gridGap="16px">
                        {(['P25', 'P50', 'P90', 'P95', 'P99'] as const).map((ysqlSlowQueryKey) => (
                          <div key={ysqlSlowQueryKey}>
                            <Typography variant="body1">
                              {translateWithFallback(`field.${ysqlSlowQueryKey}`, ysqlSlowQueryKey)}
                            </Typography>
                            <Typography variant="body2">
                              {formatQueryData(ysqlSlowQueryKey)}
                            </Typography>
                          </div>
                        ))}
                      </Box>
                    </Box>
                    {!!latencyBarGraphData?.length && (
                      <>
                        <Typography variant="body1">{t('slowQueryGraph.title')}</Typography>
                        <BarChart
                          className={classes.latencyGraph}
                          width={600}
                          height={250}
                          data={latencyBarGraphData}
                        >
                          <CartesianGrid strokeDasharray="3 3" />
                          <XAxis dataKey="label" />
                          <YAxis />
                          <Tooltip />
                          <Bar dataKey="value" fill="#CBDBFF" />
                        </BarChart>
                      </>
                    )}
                  </div>
                </TabPanel>
                <TabPanel value={QueryInfoTab.DETAILS}>
                  <div className={classes.queryMetricsContainer}>{queryDetails}</div>
                </TabPanel>
              </div>
            </TabContext>
          ) : (
            // UI fallback when there is no latency metrics support.
            <div className={classes.queryInfoTabContainer}>
              <div className={classes.queryMetricsContainer}>{queryDetails}</div>
            </div>
          )}
        </div>
      )}
    </div>
  );
};
