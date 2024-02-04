import { useTranslation, TFuncKey, Namespace } from 'react-i18next';
import { colors } from '@app/theme/variables';

export interface ChartConfig {
  title: string;
  metric: string[];
  chartDrawingType: ('line' | 'area' | 'bar')[];
  chartLabels: string[];
  strokes?: string[];
  fills?: string[];
  unitKey?: TFuncKey<Namespace>;
}

export const useChartConfig = (): Record<string, ChartConfig> => {
  const { t } = useTranslation();
  return {
    compaction: {
      title: t('clusterDetail.charts.compaction'),
      chartLabels: [t('clusterDetail.charts.bytesWritten')],
      chartDrawingType: ['line'],
      metric: ['COMPACTION_BYTES_WRITTEN_MB'],
      unitKey: 'units.MiB'
    },
    cpuUsage: {
      title: t('clusterDetail.charts.cpu'),
      chartLabels: [t('clusterDetail.charts.cpuLegendSystem'),t('clusterDetail.charts.cpuLegendUser')],
      chartDrawingType: ['line', 'line'],
      metric: ['CPU_USAGE_SYSTEM', 'CPU_USAGE_USER'],
      strokes: [colors.chartStroke.cat2, colors.common.orange],
      unitKey: 'units.percent'
    },
    cqlLatency: {
      title: t('clusterDetail.charts.ycqlLatency'),
      chartLabels: [
        t('clusterDetail.charts.select'),
        t('clusterDetail.charts.insert'),
        t('clusterDetail.charts.update'),
        t('clusterDetail.charts.delete'),
        t('clusterDetail.charts.transaction'),
        t('clusterDetail.charts.other')
      ],
      chartDrawingType: ['line', 'line', 'line', 'line', 'line', 'line'],
      metric: [
        'YCQL_SELECT_LATENCY_MS',
        'YCQL_INSERT_LATENCY_MS',
        'YCQL_UPDATE_LATENCY_MS',
        'YCQL_DELETE_LATENCY_MS',
        'YCQL_TRANSACTION_LATENCY_MS',
        'YCQL_OTHER_LATENCY_MS'
      ],
      unitKey: 'units.millisecond'
    },
    cqlOperations: {
      title: t('clusterDetail.performance.metrics.options.ycqlOperations'),
      chartLabels: [
        t('clusterDetail.charts.select'),
        t('clusterDetail.charts.insert'),
        t('clusterDetail.charts.update'),
        t('clusterDetail.charts.delete'),
        t('clusterDetail.charts.transaction'),
        t('clusterDetail.charts.other')
      ],
      chartDrawingType: ['line', 'line', 'line', 'line', 'line', 'line'],
      metric: [
        'YCQL_SELECT_OPS_PER_SEC',
        'YCQL_INSERT_OPS_PER_SEC',
        'YCQL_UPDATE_OPS_PER_SEC',
        'YCQL_DELETE_OPS_PER_SEC',
        'YCQL_TRANSACTION_OPS_PER_SEC',
        'YCQL_OTHER_OPS_PER_SEC'
      ],
      unitKey: 'units.opsSec'
    },
    cqlP99Latency: {
      title: t('clusterDetail.charts.ycqlP99Latency'),
      chartLabels: [
        t('clusterDetail.charts.select'),
        t('clusterDetail.charts.insert'),
        t('clusterDetail.charts.update'),
        t('clusterDetail.charts.delete'),
        t('clusterDetail.charts.transaction'),
        t('clusterDetail.charts.other')
      ],
      chartDrawingType: ['line', 'line', 'line', 'line', 'line', 'line'],
      metric: [
        'YCQL_P99_SELECT_LATENCY_MS',
        'YCQL_P99_INSERT_LATENCY_MS',
        'YCQL_P99_UPDATE_LATENCY_MS',
        'YCQL_P99_DELETE_LATENCY_MS',
        'YCQL_P99_TRANSACTION_LATENCY_MS',
        'YCQL_P99_OTHER_LATENCY_MS'
      ],
      unitKey: 'units.millisecond'
    },
    diskBytes: {
      title: t('clusterDetail.charts.diskBytesPerSec'),
      chartLabels: [t('clusterDetail.charts.read'), t('clusterDetail.charts.write')],
      chartDrawingType: ['line', 'line'],
      metric: ['DISK_BYTES_READ_MB_PER_SEC', 'DISK_BYTES_WRITTEN_MB_PER_SEC'],
      unitKey: 'units.MiB'
    },
    diskUsage: {
      title: t('clusterDetail.charts.disk'),
      chartLabels: [t('clusterDetail.charts.diskLegendUsed'), t('clusterDetail.charts.diskLegendProvisioned')],
      chartDrawingType: ['area', 'line'],
      metric: ['DISK_USAGE_GB', 'PROVISIONED_DISK_SPACE_GB'],
      strokes: [colors.chartStroke.cat2, colors.common.orange],
      fills: [colors.chartFill.area2],
      unitKey: 'units.GB'
    },
    memoryUsage: {
      title: t('clusterDetail.charts.memory'),
      chartLabels: [t('clusterDetail.charts.memoryLegendUsed'), t('clusterDetail.charts.memoryLegendProvisioned')],
      chartDrawingType: ['area', 'line'],
      metric: ['MEMORY_USAGE_GB', 'MEMORY_TOTAL_GB'],
      strokes: [colors.chartStroke.cat2, colors.common.orange],
      fills: [colors.chartFill.area2],
      unitKey: 'units.GB'
    },
    latency: {
      title: t('clusterDetail.charts.avLatency'),
      chartLabels: [t('clusterDetail.charts.read'), t('clusterDetail.charts.write')],
      chartDrawingType: ['line', 'line'],
      metric: ['AVERAGE_READ_LATENCY_MS', 'AVERAGE_WRITE_LATENCY_MS'],
      unitKey: 'units.millisecond'
    },
    networkBytes: {
      title: t('clusterDetail.charts.networkBytesPerSec'),
      chartLabels: [t('clusterDetail.charts.received'), t('clusterDetail.charts.transmitted')],
      chartDrawingType: ['line', 'line'],
      metric: ['NETWORK_RECEIVE_BYTES_MB_PER_SEC', 'NETWORK_TRANSMIT_BYTES_MB_PER_SEC'],
      unitKey: 'units.MiB'
    },
    networkErrors: {
      title: t('clusterDetail.charts.networkErrors'),
      chartLabels: [t('clusterDetail.charts.received'), t('clusterDetail.charts.transmitted')],
      chartDrawingType: ['line', 'line'],
      metric: ['NETWORK_RECEIVE_ERRORS_PER_SEC', 'NETWORK_TRANSMIT_ERRORS_PER_SEC']
    },
    nodeRPCs: {
      title: t('clusterDetail.charts.rpcQueueSize'),
      chartLabels: [t('clusterDetail.charts.master'), t('clusterDetail.charts.tserver'), t('clusterDetail.charts.cql')],
      chartDrawingType: ['line', 'line', 'line'],
      metric: ['RPC_QUEUE_SIZE_MASTER', 'RPC_QUEUE_SIZE_TSERVER', 'RPC_QUEUE_SIZE_CQL']
    },
    operations: {
      title: t('clusterDetail.charts.operations'),
      chartLabels: [t('clusterDetail.charts.read'), t('clusterDetail.charts.write')],
      chartDrawingType: ['line', 'line'],
      metric: ['READ_OPS_PER_SEC', 'WRITE_OPS_PER_SEC'],
      unitKey: 'units.opsSec'
    },
    remoteRPCs: {
      title: t('clusterDetail.charts.ycqlRemoteOps'),
      chartLabels: [t('clusterDetail.charts.read'), t('clusterDetail.charts.write')],
      chartDrawingType: ['line', 'line'],
      metric: ['YCQL_REMOTE_READS', 'YCQL_REMOTE_WRITES']
    },
    sqlLatency: {
      title: t('clusterDetail.charts.ysqlLatency'),
      chartLabels: [
        t('clusterDetail.charts.select'),
        t('clusterDetail.charts.insert'),
        t('clusterDetail.charts.update'),
        t('clusterDetail.charts.delete')
      ],
      chartDrawingType: ['line', 'line', 'line', 'line'],
      metric: ['YSQL_SELECT_LATENCY_MS', 'YSQL_INSERT_LATENCY_MS', 'YSQL_UPDATE_LATENCY_MS', 'YCQL_DELETE_LATENCY_MS'],
      unitKey: 'units.millisecond'
    },
    sqlOperations: {
      title: t('clusterDetail.charts.ysqlOps'),
      chartLabels: [
        t('clusterDetail.charts.select'),
        t('clusterDetail.charts.insert'),
        t('clusterDetail.charts.update'),
        t('clusterDetail.charts.delete')
      ],
      chartDrawingType: ['line', 'line', 'line', 'line'],
      metric: [
        'YSQL_SELECT_OPS_PER_SEC',
        'YSQL_INSERT_OPS_PER_SEC',
        'YSQL_UPDATE_OPS_PER_SEC',
        'YSQL_DELETE_OPS_PER_SEC'
      ],
      unitKey: 'units.opsSec'
    },
    sqlTables: {
      title: t('clusterDetail.charts.ssTablesPerNode'),
      chartLabels: [t('clusterDetail.charts.numSSTables')],
      chartDrawingType: ['line'],
      metric: ['AVERAGE_SSTABLES_PER_NODE']
    },
    totalConnections: {
      title: t('clusterDetail.charts.totalConnections'),
      chartLabels: [
        t('clusterDetail.charts.logicalConnections'),
        t('clusterDetail.charts.physicalConnections')
      ],
      chartDrawingType: ['line', 'line'],
      metric: [
        'TOTAL_LOGICAL_CONNECTIONS',
        'TOTAL_PHYSICAL_CONNECTIONS',
      ],
    },
    totalLiveNodes: {
        title: t('clusterDetail.charts.totalLiveNodes'),
        chartLabels: [t('clusterDetail.charts.liveNodes')],
        chartDrawingType: ['line'],
        metric: ['TOTAL_LIVE_NODES']
    },
    walBytes: {
      title: t('clusterDetail.charts.walBytesWritten'),
      chartLabels: [t('clusterDetail.charts.bytesLogged')],
      chartDrawingType: ['line'],
      metric: ['WAL_BYTES_WRITTEN_MB_PER_SEC'],
      unitKey: 'units.MiB'
    }
  };
};
