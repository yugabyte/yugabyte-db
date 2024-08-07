import { FC, useState } from 'react';
import clsx from 'clsx';
import { get, isEmpty } from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { Box, Typography, Link } from '@material-ui/core';
import { YBButton, YBTooltip } from '../../components';
import { YBTable } from '../../../components/common/YBTable';
import { YBLoading } from '../../../components/common/indicators';
import { YBLabelWithIcon } from '../../../components/common/descriptors';
import { ExportLogModalForm } from './components/ExportLogModalForm';
import { DeleteTelProviderModal } from './components/DeleteTelProviderModal';
import { api, QUERY_KEY } from '../../utils/api';
import { universeQueryKey } from '../../helpers/api';
import { ExportLogResponse, TPItem } from './utils/types';
import { TelemetryProviderMin } from './components/DeleteTelProviderModal';
import { getLinkedUniverses } from './utils/helpers';
import { TP_FRIENDLY_NAMES } from './utils/constants';

//styles
import { usePillStyles } from '../../styles/styles';
import { exportLogStyles } from './utils/ExportLogStyles';
import styles from '../../../components/configRedesign/providerRedesign/ProviderList.module.scss';

//icons
import AuditBackupIcon from '../../assets/backup.svg';
import EllipsisIcon from '../../assets/ellipsis.svg';

interface ExportLogProps {}

export const ExportLog: FC<ExportLogProps> = () => {
  const classes = exportLogStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation();
  const [openExportModal, setOpenExportModal] = useState(false);
  const [exportModalProps, setExportModalProps] = useState<TPItem | null>(null);
  const [openDeleteModal, setDeleteModal] = useState(false);
  const [deleteModalProps, setDeleteModalProps] = useState<TelemetryProviderMin | null>(null);

  const { data, isLoading: logslistLoading, refetch } = useQuery<ExportLogResponse[]>(
    [QUERY_KEY.getAllTelemetryProviders],
    () => api.getAllTelemetryProviders()
  );

  const {
    data: universeList,
    isLoading: universeListLoading,
    isFetching
  } = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList());

  if (logslistLoading || universeListLoading || isFetching) return <YBLoading />;

  const finalData = data?.map((logData) => {
    const linkedUniverses = getLinkedUniverses(logData?.uuid, universeList || []);
    return { ...logData, type: logData?.config?.type || '', linkedUniverses };
  });

  const handleRowClick = (row: TPItem) => {
    setExportModalProps(row);
    setOpenExportModal(true);
  };

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
    const displayCount = 4;
    const beforeDisplayCount = universeNameArr.slice(0, displayCount);
    let afterDisplayCount = [];
    if (universeNameArr.length > displayCount) {
      afterDisplayCount = universeNameArr.slice(displayCount);
    }

    return row.linkedUniverses.length ? (
      <Box display="flex" flexDirection={'row'}>
        {beforeDisplayCount.map((un: string) => {
          return (
            <div className={clsx(pillClasses.pill, classes.mr4)} key={un}>
              {un}
            </div>
          );
        })}
        {afterDisplayCount.length > 0 && (
          <YBTooltip
            title={
              <>
                {afterDisplayCount.map((un: string) => (
                  <span key={un}>
                    {un}
                    <br />
                  </span>
                ))}
              </>
            }
          >
            <div className={clsx(pillClasses.pill)}>+{afterDisplayCount.length}</div>
          </YBTooltip>
        )}
      </Box>
    ) : (
      <span>-</span>
    );
  };

  const formatActions = (cell: any, row: any) => {
    return (
      <Dropdown id="table-actions-dropdown" pullRight onClick={(e) => e.stopPropagation()}>
        <Dropdown.Toggle noCaret>
          <img src={EllipsisIcon} alt="more" className="ellipsis-icon" />
        </Dropdown.Toggle>
        <Dropdown.Menu>
          <MenuItem
            eventKey="1"
            onSelect={() => {
              setDeleteModalProps({ name: row.name, uuid: row.uuid });
              setDeleteModal(true);
            }}
            data-testid="ExportLog-DeleteConfiguration"
            disabled={!isEmpty(row.linkedUniverses)}
          >
            <YBLabelWithIcon icon="fa fa-trash">{t('exportAuditLog.deleteConfig')}</YBLabelWithIcon>
          </MenuItem>
        </Dropdown.Menu>
      </Dropdown>
    );
  };
  return (
    <Box display="flex" flexDirection="column" width="100%" p={0.25}>
      <Box mb={4}>
        <Typography className={classes.mainTitle}>
          {t('exportAuditLog.exportConfigForLogs')}
        </Typography>
      </Box>
      {!isEmpty(finalData) ? (
        <Box className={classes.exportListContainer}>
          <Box display={'flex'} flexDirection={'row'} justifyContent={'flex-end'}>
            <YBButton
              variant="primary"
              size="large"
              onClick={() => setOpenExportModal(true)}
              data-testid="ExportLog-AddConfig"
            >
              <i className="fa fa-plus" />
              {t('exportAuditLog.addConfiguration')}
            </YBButton>
          </Box>
          <Box mt={4} width="100%" height="100%">
            <YBTable
              data={finalData || []}
              options={{
                onRowClick: handleRowClick
              }}
              hover
              pagination
              height="100%"
            >
              <TableHeaderColumn
                width="250"
                dataField="name"
                isKey
                dataSort
                dataFormat={(cell) => <span>{cell}</span>}
              >
                <span>{t('exportAuditLog.exportName')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn
                width="200"
                dataField="type"
                dataSort
                dataFormat={(cell) => <span>{TP_FRIENDLY_NAMES[cell]}</span>}
              >
                <span>{t('exportAuditLog.exportTo')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn dataFormat={formatUsage} width="200">
                {t('exportAuditLog.usageHeader')}
              </TableHeaderColumn>
              <TableHeaderColumn dataFormat={formatUniverseList}>
                {t('exportAuditLog.assignedUniverses')}
              </TableHeaderColumn>
              <TableHeaderColumn
                columnClassName={styles.exportActionsColumn}
                dataFormat={formatActions}
                width="70"
              ></TableHeaderColumn>
            </YBTable>
          </Box>
        </Box>
      ) : (
        <Box className={classes.emptyContainer}>
          <Box display={'flex'} flexDirection={'column'} alignItems={'center'}>
            <img src={AuditBackupIcon} alt="--" height={48} width={48} />
            <Box mt={3} mb={2}>
              <YBButton
                variant="primary"
                size="large"
                onClick={() => setOpenExportModal(true)}
                data-testid="ExportLog-CreateExport"
              >
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
                {/* <Link underline="always"></Link> */}
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
          formProps={exportModalProps}
          onClose={() => {
            setExportModalProps(null);
            setOpenExportModal(false);
            refetch();
          }}
        />
      )}
      {openDeleteModal && deleteModalProps && (
        <DeleteTelProviderModal
          open={openDeleteModal}
          onClose={() => {
            setDeleteModal(false);
            setDeleteModalProps(null);
            refetch();
          }}
          telemetryProviderProps={deleteModalProps}
        />
      )}
    </Box>
  );
};
