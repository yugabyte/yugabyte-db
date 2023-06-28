/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useState } from 'react';
import clsx from 'clsx';
import { Bar, BarChart, CartesianGrid, Tooltip, XAxis, YAxis } from 'recharts';
import { Box, makeStyles, Tab, Typography, useTheme } from '@material-ui/core';
import { TabContext, TabList, TabPanel } from '@material-ui/lab';
import { useTranslation } from 'react-i18next';

import { CodeBlock } from './CodeBlock';
import { DURATION_FIELDS, DURATION_FIELD_DECIMALS } from './helpers/constants';
import { YSQLSlowQuery, YSQLSlowQueryPrimativeFields } from './helpers/types';
import { adaptHistogramData } from './helpers/utils';
import { parseFloatIfDefined } from '../xcluster/ReplicationUtils';

interface QueryInfoSidePanelProps {
  queryData: YSQLSlowQuery;
  visible: boolean;
  onHide: () => void;
}

/**
 * This constant stores the order of the `YSQLSlowQuery` fields to display to the user.
 */
const YSQL_SLOW_QUERY_DETAIL_FIELDS: YSQLSlowQueryPrimativeFields[] = [
  'calls',
  'rows',
  'datname',
  'userid',
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
export const QueryInfoSidePanel = ({ queryData, visible, onHide }: QueryInfoSidePanelProps) => {
  const [currentTab, setCurrentTab] = useState<QueryInfoTab>(DEFAULT_TAB);
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
  const formatQueryData = (ysqlSlowQueryKey: YSQLSlowQueryPrimativeFields): string => {
    if (queryData[ysqlSlowQueryKey] === undefined) {
      return NO_DATA_PLACEHOLDER_TEXT;
    }

    if ((DURATION_FIELDS as readonly YSQLSlowQueryPrimativeFields[]).includes(ysqlSlowQueryKey)) {
      const duration = parseFloatIfDefined(
        queryData[ysqlSlowQueryKey as typeof DURATION_FIELDS[number]]
      );
      return duration === undefined || isNaN(duration)
        ? NO_DATA_PLACEHOLDER_TEXT
        : `${duration?.toFixed(DURATION_FIELD_DECIMALS)} ms`;
    }
    return queryData[ysqlSlowQueryKey].toString();
  };

  const latencyBarGraphData = queryData?.yb_latency_histogram
    ? adaptHistogramData(queryData.yb_latency_histogram)
    : [];
  return (
    <div className={`side-panel ${!visible ? 'panel-hidden' : ''}`}>
      <div className={classes.header}>
        <Typography variant="h4">{t('title')}</Typography>
        <i className={clsx('fa fa-close', classes.headerIcon)} onClick={onHide} />
      </div>
      {queryData && (
        <div className={classes.content}>
          <CodeBlock code={queryData.query} />
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
                      {(['max_time', 'min_time', 'mean_time'] as const).map((ysqlSlowQueryKey) => (
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
                  {!!latencyBarGraphData.length && (
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
                <div className={classes.queryMetricsContainer}>
                  {YSQL_SLOW_QUERY_DETAIL_FIELDS.map((ysqlSlowQueryKey) => {
                    return (
                      <div className={classes.queryMetricRow} key={ysqlSlowQueryKey}>
                        <Typography
                          className={classes.queryMetricLabel}
                          variant="body2"
                          color="textSecondary"
                        >
                          {translateWithFallback(`field.${ysqlSlowQueryKey}`, ysqlSlowQueryKey)}
                        </Typography>
                        <Typography variant="body2">{formatQueryData(ysqlSlowQueryKey)}</Typography>
                      </div>
                    );
                  })}
                </div>
              </TabPanel>
            </div>
          </TabContext>
        </div>
      )}
    </div>
  );
};
