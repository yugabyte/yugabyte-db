import { FC } from 'react';
import { RelocationRow } from '@app/redesign/features-v2/onboarding/universe-revamp/modals/WhereThingsMovedModal';
import { WhereThingsMovedTip } from '@app/redesign/features-v2/onboarding/universe-revamp/tips/WhereThingsMovedTip';

const METRICS_AND_QUERIES_ROWS: RelocationRow[] = [
  { thenKey: 'metrics', nowPathKey: 'nowClassicMetrics' },
  { thenKey: 'queries', nowPathKey: 'nowQueries' }
];

const CLUSTER_LOAD_ROWS: RelocationRow[] = [
  { thenKey: 'performanceClusterLoad', nowPathKey: 'nowClusterLoad' },
  { thenKey: 'performanceMetrics', nowPathKey: 'nowAdvancedMetrics' }
];

const DATABASES_ROW: RelocationRow = {
  thenKey: 'performanceDatabases',
  nowPathKey: 'nowDatabases'
};

interface MonitoringMovedTipProps {
  showClusterLoadRows: boolean;
  showDatabasesRow: boolean;
}

export const MonitoringMovedTip: FC<MonitoringMovedTipProps> = ({
  showClusterLoadRows,
  showDatabasesRow
}) => (
  <WhereThingsMovedTip
    i18nKeyPrefix="onBoarding.monitoringMovedTip"
    modalProps={{
      i18nKeyPrefix: 'onBoarding.monitoringMovedModal',
      rows: [
        ...METRICS_AND_QUERIES_ROWS,
        ...(showClusterLoadRows ? CLUSTER_LOAD_ROWS : []),
        ...(showDatabasesRow ? [DATABASES_ROW] : [])
      ],
      nowPrefixKey: 'monitoring',
      showFindOutMore: false
    }}
  />
);
