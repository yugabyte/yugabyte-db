import InfoIcon from '@app/assets/info.svg';
import { Box, Grid, makeStyles, Typography } from '@material-ui/core';
import React, { FC, useEffect, useState, Fragment } from 'react';
// import { useRouteMatch } from 'react-router-dom';
import * as Yup from 'yup';
import { useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { useTranslation } from 'react-i18next';
import { YBCheckboxField, YBModal } from '@app/components';
// import { useRuntimeConfig } from '@app/helpers';

// SS: temp limitation for max number of metrics visible at a time, exceeding it could crash the apiserver; CLOUDGA-3613
const MAX_METRICS_NUM = 4;
const MIN_METRICS_NUM = 1;

export interface MetricsQueryParams {
  showGraph: string[];
}

interface MetricsOptionsModalProps {
  visibleGraphList: string[];
  onChange: (visibleGraphList: string[]) => void;
  nodeName: string | undefined;
  open: boolean;
  setVisibility: (open: boolean) => void;
  metricsQueryParams: MetricsQueryParams;
}

interface ChartType {
  name: string;
  label: string;
  separator?: string;
}

const useStyles = makeStyles((theme) => ({
  error: {
    display: 'flex',
    alignItems: 'center',
    color: theme.palette.error[500],
    '& svg': {
      marginRight: theme.spacing(0.5)
    }
  }
}));

export const MetricsOptionsModal: FC<MetricsOptionsModalProps> = ({
  onChange,
  open,
  setVisibility,
  metricsQueryParams
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  // const { params } = useRouteMatch<App.RouteParams>();

  const getDefaultValues = () => {
    const values: Record<string, boolean> = {
      operations: false,
      latency: false,
      sqlOperations: false,
      sqlLatency: false,
      cqlLatency: false,
      cpuUsage: false,
      cqlOperations: false,
      cqlP99Latency: false,
      diskUsage: false,
      memoryUsage: false,
      networkBytes: false,
      diskBytes: false,
      networkErrors: false,
      remoteRPCs: false,
      overallRpcs: false,
      avgSSTables: false,
      walBytes: false,
      compaction: false
    };
    if (metricsQueryParams?.showGraph) {
      metricsQueryParams?.showGraph?.forEach((metric) => {
        if (!metric) return;
        values[metric] = true;
      });
      return values;
    }
    return { ...values, sqlLatency: true, sqlOperations: true, cpuUsage: true, diskUsage: true }; //show these graph by default
  };

  // const { data: globalConfig } = useRuntimeConfig();
  // const { data: accountConfig } = useRuntimeConfig(params.accountId);
  // const maxChartsGlobalVal = globalConfig?.MaxMetricsCharts ?? MAX_METRICS_NUM;
  // const maxChartsAccountVal = accountConfig?.MaxMetricsCharts ?? MAX_METRICS_NUM;
  const maxChartsGlobalVal = MAX_METRICS_NUM;
  const maxChartsAccountVal = MAX_METRICS_NUM;


  let MAX_METRIC_CHARTS: number;
  if (maxChartsAccountVal) MAX_METRIC_CHARTS = +maxChartsAccountVal;
  else if (maxChartsGlobalVal) MAX_METRIC_CHARTS = +maxChartsGlobalVal;
  else MAX_METRIC_CHARTS = MAX_METRICS_NUM;

  // perform form-level validation
  const validationSchema = Yup.object().test((value) => {
    const chartsSelected = Object.keys(value).filter((key) => value[key]).length;
    return chartsSelected >= MIN_METRICS_NUM && chartsSelected <= MAX_METRIC_CHARTS;
  });

  const [originalValues, setOriginalValues] = useState(getDefaultValues());
  const { control, handleSubmit, formState, setValue, reset } = useForm({
    mode: 'onChange',
    resolver: yupResolver(validationSchema),
    defaultValues: originalValues
  });

  const chartTypeList: Record<string, ChartType> = {
    operations: {
      name: 'operations',
      label: t('clusterDetail.performance.metrics.options.operations'),
      separator: t('clusterDetail.performance.metrics.options.overall')
    },
    latency: {
      name: 'latency',
      label: t('clusterDetail.performance.metrics.options.latency')
    },
    cpuUsage: {
      name: 'cpuUsage',
      label: t('clusterDetail.performance.metrics.options.cpu'),
      separator: t('clusterDetail.performance.metrics.options.general')
    },
    memoryUsage: {
      name: 'memoryUsage',
      label: t('clusterDetail.performance.metrics.options.memoryUsage')
    },
    diskUsage: {
      name: 'diskUsage',
      label: t('clusterDetail.performance.metrics.options.diskUsage')
    },
    networkBytes: {
      name: 'networkBytes',
      label: t('clusterDetail.performance.metrics.options.networkBytesPerSec')
    },
    diskBytes: {
      name: 'diskBytes',
      label: t('clusterDetail.performance.metrics.options.diskBytesPerSec')
    },
    networkErrors: {
      name: 'networkErrors',
      label: t('clusterDetail.performance.metrics.options.networkErrors')
    },
    remoteRPCs: {
      name: 'remoteRPCs',
      label: t('clusterDetail.performance.metrics.options.remoteRpcs'),
      separator: t('clusterDetail.performance.metrics.options.advanced')
    },
    overallRpcs: {
      name: 'nodeRPCs',
      label: t('clusterDetail.performance.metrics.options.overallRpcs')
    },
    sqlOperations: {
      name: 'sqlOperations',
      label: t('clusterDetail.performance.metrics.options.ysqlOperations'),
      separator: t('clusterDetail.performance.metrics.options.ysql')
    },
    sqlLatency: {
      name: 'sqlLatency',
      label: t('clusterDetail.performance.metrics.options.ysqlLatency')
    },
    cqlOperations: {
      name: 'cqlOperations',
      label: t('clusterDetail.performance.metrics.options.ycqlOperations'),
      separator: t('clusterDetail.performance.metrics.options.ycql')
    },
    cqlLatency: {
      name: 'cqlLatency',
      label: t('clusterDetail.performance.metrics.options.ycqlLatency')
    },
    cqlP99Latency: {
      name: 'cqlP99Latency',
      label: t('clusterDetail.performance.metrics.options.ycqlLatencyP99')
    },
    walBytes: {
      name: 'walBytes',
      label: t('clusterDetail.performance.metrics.options.walBytes'),
      separator: t('clusterDetail.performance.metrics.options.tabletServer')
    },
    compaction: {
      name: 'compaction',
      label: t('clusterDetail.performance.metrics.options.compaction'),
      separator: t('clusterDetail.performance.metrics.options.docdb')
    },
    avgSSTables: {
      name: 'sqlTables',
      label: t('clusterDetail.performance.metrics.options.avgSSTables')
    }
  };

  useEffect(() => {
    if (metricsQueryParams?.showGraph) {
      metricsQueryParams?.showGraph?.forEach((metric) => {
        if (!metric) return;
        setValue(metric, true);
      });
    } else {
      setValue('cpuUsage', true);
      setValue('sqlLatency', true);
      setValue('sqlOperations', true);
      setValue('diskUsage', true);
    }
  }, [metricsQueryParams?.showGraph, setValue]);

  const applyMetricsOptions = handleSubmit((formData) => {
    onChange(Object.keys(formData).filter((key) => formData[key]));
    setOriginalValues(formData);
    setVisibility(false);
  });

  const handleClose = () => {
    reset(originalValues);
    setVisibility(false);
  };

  const leftList = [
    'operations',
    'latency',
    'cpuUsage',
    'diskUsage',
    'memoryUsage',
    'networkBytes',
    'diskBytes',
    'networkErrors',
    'remoteRPCs',
    'overallRpcs'
  ];
  const rightList = [
    'sqlOperations',
    'sqlLatency',
    'cqlOperations',
    'cqlLatency',
    'cqlP99Latency',
    'walBytes',
    'compaction',
    'avgSSTables'
  ];
  return (
    <YBModal
      open={open}
      title={t('clusterDetail.performance.metrics.metricsOptions')}
      onClose={handleClose}
      onSubmit={applyMetricsOptions}
      enableBackdropDismiss
      titleSeparator
      actionsInfo={
        !formState.isValid && (
          <div className={classes.error} data-testId="errorMetricsOption">
            <InfoIcon />
            {t('validations.maxPerfMetrics', { max: MAX_METRIC_CHARTS, min: MIN_METRICS_NUM })}
          </div>
        )
      }
      submitLabel={t('common.apply')}
      submitTestId="btnApplyMetricsOptions"
      cancelLabel={t('common.cancel')}
      cancelTestId={'btnCloseMetricsOptions'}
    >
      <Box mt={1} mb={2}>
        <Typography variant="body2" color="textSecondary">
          {t('clusterDetail.performance.metrics.displayMetrics')}
        </Typography>
      </Box>
      <Grid container>
        <Grid item xs={6}>
          {leftList.map((chartName: string, index: number) => {
            const chartType = chartTypeList[chartName];
            const keyName = Object.keys(chartTypeList)[index];
            return (
              <Fragment key={chartName}>
                {chartType?.separator && (
                  <Typography variant="body2" color="textSecondary">
                    {chartType.separator}
                  </Typography>
                )}
                <Box>
                  <YBCheckboxField
                    name={chartType.name}
                    label={chartType.label}
                    control={control}
                    inputProps={{
                      'data-testid': keyName
                    }}
                  />
                </Box>
              </Fragment>
            );
          })}
        </Grid>
        <Grid item xs={6}>
          {rightList.map((chartName: string) => {
            const chartType = chartTypeList[chartName];
            return (
              <Fragment key={chartName}>
                {chartType?.separator && (
                  <Typography variant="body2" color="textSecondary">
                    {chartType.separator}
                  </Typography>
                )}
                <Box>
                  <YBCheckboxField name={chartType.name} label={chartType.label} control={control} />
                </Box>
              </Fragment>
            );
          })}
        </Grid>
      </Grid>
    </YBModal>
  );
};
