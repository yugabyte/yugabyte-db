import { FC } from 'react';
import _ from 'lodash';
import { useQuery, useQueries } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, Typography, Link } from '@material-ui/core';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBLoading } from '../../../../../components/common/indicators';
import { QUERY_KEY, api } from '../../../../utils/api';
import { replicationSlotStyles } from './ReplicationSlotStyles';
import { fetchCDCMetrics } from './helper';
import { ReplicationSlotResponse } from './types';
//icons
import DatabaseEvent from '../../../../assets/database-event.svg';
import Flash from '../../../../assets/flash.svg';
interface ReplicationTableProps {
  universeUUID: string;
  nodePrefix: string;
}

export const ReplicationSlotTable: FC<ReplicationTableProps> = ({ universeUUID, nodePrefix }) => {
  const classes = replicationSlotStyles();
  const { t } = useTranslation();

  const { data: replicationSlotData, isLoading } = useQuery<ReplicationSlotResponse>(
    [QUERY_KEY.getReplicationSlots, universeUUID],
    () => api.getReplicationSlots(universeUUID)
  );

  const metricsQuery = useQueries(
    (replicationSlotData?.replicationSlots || []).map((d) => ({
      queryKey: d.streamID,
      queryFn: () => fetchCDCMetrics(d.streamID, nodePrefix),
      enabled: !!replicationSlotData?.replicationSlots?.length
    }))
  );

  const getCurrentLag = (sID: string) => {
    const streamResult = metricsQuery.find((m) => m.data?.streamID === sID);
    if (!streamResult) return 'n/a';
    else return _.last(streamResult?.data?.cdcsdk_sent_lag_micros?.data[0]?.y);
  };

  if (isLoading) return <YBLoading />;
  if (!!replicationSlotData?.replicationSlots?.length)
    return (
      <Box display="flex" flexDirection="column" width="100%">
        <Typography>{replicationSlotData?.replicationSlots?.length} Replication Slots</Typography>
        <Box mt={3} width="100%" height="700px">
          <BootstrapTable
            data={replicationSlotData?.replicationSlots}
            height={'690px'}
            tableStyle={{
              padding: '24px',
              borderRadius: '0px 10px 10px 10px',
              border: '0px solid  #DDE1E6',
              background: '#FFF',
              boxShadow: '0px 2px 10px 0px rgba(0, 0, 0, 0.15)',
              overflow: 'scroll'
            }}
            headerStyle={{
              fontSize: '12px',
              fontWeight: 600,
              color: '#0B1117'
            }}
          >
            <TableHeaderColumn
              width={'35%'}
              dataField="slotName"
              dataSort
              dataFormat={(cell) => <span>{cell}</span>}
            >
              <span>Name</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width={'20%'}
              dataField="state"
              dataSort
              dataFormat={(cell) => <span>{cell}</span>}
            >
              <span>Status</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width={'25%'}
              dataField="databaseName"
              dataSort
              dataFormat={(cell) => <span>{cell}</span>}
            >
              <span>Database</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width={'20%'}
              dataField="streamID"
              dataSort
              dataFormat={(cell) => <span>{getCurrentLag(cell)} ms</span>}
              isKey
            >
              <span>Current Lag</span>
            </TableHeaderColumn>
          </BootstrapTable>
        </Box>
      </Box>
    );
  else
    return (
      <Box className={classes.emptyContainer}>
        <img src={DatabaseEvent} alt="--" />
        <Box mt={2} mb={2}>
          <Typography className={classes.emptyContainerTitle}>
            {t('cdc.emptyContainerTitle')}{' '}
          </Typography>
        </Box>
        <Typography variant="body2" className={classes.emptyContainerSubtitle}>
          {t('cdc.emptyContainerSubtitle')}
        </Typography>
        <Box display="flex" flexDirection="row" mt={2} alignItems={'end'}>
          <img src={Flash} alt="--" /> &nbsp;
          <Link
            component={'button'}
            underline="always"
            onClick={() => {}}
            className={classes.emptyContainerLink}
          >
            {t('cdc.emptyContainerLink')}
          </Link>
        </Box>
      </Box>
    );
};
