import { FC, useState } from 'react';
import { get, isEmpty } from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { Box, Typography, Link, IconButton, Menu, MenuItem } from '@material-ui/core';
import { YBButton } from '../../components';
import { YBLoading } from '../../../components/common/indicators';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { YBTable } from '../../../components/common/YBTable';
import { ExportLogModalForm } from './components/ExportLogModalForm';
import { api, QUERY_KEY } from '../../utils/api';
import { universeQueryKey } from '../../helpers/api';
import { ExportLogResponse } from './utils/types';
import { getLinkedUniverses } from './utils/helpers';
import { TP_FRIENDLY_NAMES } from './utils/constants';

//styles
import { usePillStyles } from '../../styles/styles';
import { exportLogStyles } from './utils/ExportLogStyles';

//icons
import AuditBackupIcon from '../../assets/backup.svg';
import { ReactComponent as EllipsisIcon } from '../../assets/ellipsis.svg';

interface ExportLogProps {}

export const ExportLog: FC<ExportLogProps> = () => {
  const classes = exportLogStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation();
  const [anchorEl, setAnchorEl] = useState(null);
  const [openExportModal, setOpenExportModal] = useState(false);

  const { data, isLoading: logslistLoading, refetch } = useQuery<ExportLogResponse[]>(
    [QUERY_KEY.getAllTelemetryProviders],
    () => api.getAllTelemetryProviders()
  );

  const { data: universeList, isLoading: universeListLoading } = useQuery(
    universeQueryKey.ALL,
    () => api.fetchUniverseList()
  );

  if (logslistLoading || universeListLoading) return <YBLoading />;

  const finalData = data?.map((logData) => {
    const linkedUniverses = getLinkedUniverses(logData?.uuid, universeList || []);
    return { ...logData, type: logData?.config?.type || '', linkedUniverses };
  });

  const formatUsage = (_: unknown, row: any) => {
    return row.linkedUniverses.length ? (
      <Box display="flex" gridGap="5px" alignItems="center">
        <Typography variant="body2">{t('exportAuditLog.inUse')}</Typography>
        <div className={pillClasses.pill}>{row.linkedUniverses.length}</div>
      </Box>
    ) : (
      <Typography variant="body2">{t('exportAuditLog.notInUse')}</Typography>
    );
  };

  const formatUniverseList = (_: unknown, row: any) => {
    const universeNameArr = row.linkedUniverses.map((ul: any) =>
      get(ul, 'linkedClusters[0].userIntent.universeName')
    );
    return row.linkedUniverses.length ? (
      <Box display="flex" flexDirection={'row'}>
        {universeNameArr.map((un: string) => {
          return <div className={pillClasses.pill}>{un}</div>;
        })}
      </Box>
    ) : (
      <span>--</span>
    );
  };

  const handleActionClick = (e: any) => {
    setAnchorEl(e.currentTarget);
  };

  const handleClose = () => {
    setAnchorEl(null);
  };

  const formatActions = (_: unknown, row: any) => {
    return (
      <div key={row.name}>
        <IconButton
          aria-label="more"
          aria-controls="long-menu"
          aria-haspopup="true"
          onClick={handleActionClick}
        >
          <EllipsisIcon />
        </IconButton>
        <Menu
          id="long-menu"
          anchorEl={anchorEl}
          keepMounted
          open={Boolean(anchorEl)}
          onClose={handleClose}
        >
          <MenuItem key={row.name} disabled={isEmpty(row.linkedUniverses)} onClick={handleClose}>
            {t('exportAuditLog.deleteConfiguration')}
          </MenuItem>
        </Menu>
      </div>
    );
  };

  return (
    <Box display="flex" flexDirection="column" width="100%" p={0.25}>
      <Box mb={4}>
        <Typography className={classes.mainTitle}>
          {t('exportAuditLog.exportConfigForLogs')}
        </Typography>
      </Box>
      {(finalData || []).length > 0 ? (
        <Box className={classes.exportListContainer}>
          <Box display={'flex'} flexDirection={'row'} justifyContent={'flex-end'}>
            <YBButton variant="primary" size="large" onClick={() => setOpenExportModal(true)}>
              <i className="fa fa-plus" />
              {t('exportAuditLog.addConfiguration')}
            </YBButton>
          </Box>
          <Box mt={4} width="100%" height="100%">
            <YBTable data={finalData || []} hover={false}>
              <TableHeaderColumn
                width="20%"
                dataField="name"
                isKey
                dataSort
                dataFormat={(cell) => <span>{cell}</span>}
              >
                <span>{t('exportAuditLog.exportName')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn
                width="20%"
                dataField="type"
                dataSort
                dataFormat={(cell) => <span>{TP_FRIENDLY_NAMES[cell]}</span>}
              >
                <span>{t('exportAuditLog.exportTo')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn dataFormat={formatUsage}>
                {t('exportAuditLog.usageHeader')}
              </TableHeaderColumn>
              <TableHeaderColumn dataFormat={formatUniverseList}>
                {t('exportAuditLog.assignedUniverses')}
              </TableHeaderColumn>
              <TableHeaderColumn dataFormat={formatActions} width="70" />
            </YBTable>
          </Box>
        </Box>
      ) : (
        <Box className={classes.emptyContainer}>
          <Box display={'flex'} flexDirection={'column'} alignItems={'center'}>
            <img src={AuditBackupIcon} alt="--" height={48} width={48} />
            <Box mt={3} mb={2}>
              <YBButton variant="primary" size="large" onClick={() => setOpenExportModal(true)}>
                {t('exportAuditLog.createExport')}
              </YBButton>
            </Box>
            <Typography variant="body2">{t('exportAuditLog.emptyMsg1')}</Typography>
            <br /> <br />
            <Typography variant="body2">
              <Trans>
                {t('exportAuditLog.learnExport')}
                <Link
                  target="_blank"
                  underline="always"
                  href="https://docs.yugabyte.com/preview/secure/audit-logging/audit-logging-ysql/"
                ></Link>
                <Link underline="always"></Link>
              </Trans>
            </Typography>
          </Box>
          <Box>
            <Typography variant="body2">
              <Trans i18nKey={'exportAuditLog.emptyExportNote'} />
            </Typography>
          </Box>
        </Box>
      )}
      {openExportModal && (
        <ExportLogModalForm
          open={openExportModal}
          onClose={() => {
            setOpenExportModal(false);
            refetch();
          }}
        />
      )}
    </Box>
  );
};
