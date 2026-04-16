import { useState } from 'react';
import { isEmpty } from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { Box, Typography, Link } from '@material-ui/core';
import clsx from 'clsx';

import { YBButton, YBTooltip } from '../../components';
import { YBTable } from '../../../components/common/YBTable';
import { YBLoading } from '../../../components/common/indicators';
import { YBLabelWithIcon } from '../../../components/common/descriptors';
import { CreateTelemetryProviderConfigSidePanel } from './CreateTelemetryProviderConfigSidePanel';
import { DeleteTelemetryProviderConfigModal } from './DeleteTelemetryProviderConfigModal';
import { api, telemetryProviderQueryKey, universeQueryKey } from '../../helpers/api';
import { TelemetryProviderItem } from './types';
import { TelemetryProviderMin } from './DeleteTelemetryProviderConfigModal';
import { TelemetryType, TP_FRIENDLY_NAMES } from './constants';
import { getIsTelemetryProviderConfigInUse, getLinkedUniverses } from './utils';

//RBAC
import { ApiPermissionMap } from '../rbac/ApiAndUserPermMapping';
import { RbacValidator } from '../rbac/common/RbacApiPermValidator';

//styles
import { usePillStyles } from '../../styles/styles';
import { useExportTelemetryStyles } from './styles';
import styles from '../../../components/configRedesign/providerRedesign/ProviderList.module.scss';

//icons
import AuditBackupIcon from '../../assets/backup.svg?img';
import EllipsisIcon from '../../assets/ellipsis.svg?img';

const TRANSLATION_KEY_PREFIX = 'exportTelemetry';

export const ExportTelemetryConfigurations = () => {
  const classes = useExportTelemetryStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const [openExportModal, setOpenExportModal] = useState(false);
  const [exportModalProps, setExportModalProps] = useState<TelemetryProviderItem | null>(null);
  const [openDeleteModal, setDeleteModal] = useState(false);
  const [deleteModalProps, setDeleteModalProps] = useState<TelemetryProviderMin | null>(null);

  const { data, isLoading: logslistLoading, refetch } = useQuery(
    telemetryProviderQueryKey.list(),
    () => api.fetchTelemetryProviderList()
  );

  const {
    data: universeList,
    isLoading: universeListLoading,
    isFetching
  } = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList());

  if (logslistLoading || universeListLoading || isFetching) return <YBLoading />;

  const telemetryProviderTableData: TelemetryProviderItem[] =
    data?.map((telemetryProvider) => {
      const linkedUniverses = getLinkedUniverses(telemetryProvider.uuid, universeList ?? []);
      return { ...telemetryProvider, type: telemetryProvider?.config?.type ?? '', linkedUniverses };
    }) ?? [];

  const handleRowClick = (row: TelemetryProviderItem) => {
    setExportModalProps(row);
    setOpenExportModal(true);
  };

  const formatLogExporterUsage = (_: unknown, row: TelemetryProviderItem) => {
    return formatUsage(TelemetryType.LOGS, row);
  };

  const formatMetricsExporterUsage = (_: unknown, row: TelemetryProviderItem) => {
    return formatUsage(TelemetryType.METRICS, row);
  };

  const formatUsage = (
    telemetryType: TelemetryType,
    telemetryProviderItem: TelemetryProviderItem
  ) => {
    const universesConfiguredForTelemetryExport =
      telemetryType === TelemetryType.LOGS
        ? telemetryProviderItem.linkedUniverses.universesWithLogExporter
        : telemetryProviderItem.linkedUniverses.universesWithMetricsExporter;
    return universesConfiguredForTelemetryExport.length ? (
      <Box display="flex" gridGap="5px" alignItems="center">
        <Typography variant="body2">{t('inUse')}</Typography>
        <YBTooltip
          title={
            <>
              <ul className={classes.universeList}>
                {universesConfiguredForTelemetryExport.map((universe) => (
                  <li key={universe.universeUUID}>{universe.name}</li>
                ))}
              </ul>
            </>
          }
        >
          <div className={clsx(pillClasses.pill, pillClasses.small, pillClasses.metadataWhite)}>
            {universesConfiguredForTelemetryExport.length}
          </div>
        </YBTooltip>
      </Box>
    ) : (
      <Typography variant="body2">{t('notInUse')}</Typography>
    );
  };

  const formatActions = (cell: any, telemetryProviderItem: TelemetryProviderItem) => {
    return (
      <Dropdown id="table-actions-dropdown" pullRight onClick={(e) => e.stopPropagation()}>
        <Dropdown.Toggle noCaret>
          <img src={EllipsisIcon} alt="more" className="ellipsis-icon" />
        </Dropdown.Toggle>
        <Dropdown.Menu>
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.DELETE_TELEMETRY_PROVIDER_BY_ID}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <MenuItem
              eventKey="1"
              onSelect={() => {
                setDeleteModalProps({
                  name: telemetryProviderItem.name,
                  uuid: telemetryProviderItem.uuid ?? ''
                });
                setDeleteModal(true);
              }}
              data-testid="ExportLog-DeleteConfiguration"
              disabled={getIsTelemetryProviderConfigInUse(telemetryProviderItem)}
            >
              <YBLabelWithIcon icon="fa fa-trash">{t('deleteConfig')}</YBLabelWithIcon>
            </MenuItem>
          </RbacValidator>
        </Dropdown.Menu>
      </Dropdown>
    );
  };
  return (
    <Box display="flex" flexDirection="column" width="100%" p={0.25}>
      <Box mb={4}>
        <Typography className={classes.mainTitle}>{t('heading')}</Typography>
      </Box>
      {!isEmpty(telemetryProviderTableData) ? (
        <Box className={classes.exportListContainer}>
          <Box display={'flex'} flexDirection={'row'} justifyContent={'flex-end'}>
            <RbacValidator accessRequiredOn={ApiPermissionMap.CREATE_TELEMETRY_PROVIDER} isControl>
              <YBButton
                variant="primary"
                size="large"
                onClick={() => setOpenExportModal(true)}
                data-testid="ExportLog-AddConfig"
              >
                <i className="fa fa-plus" />
                {t('addConfiguration')}
              </YBButton>
            </RbacValidator>
          </Box>
          <Box mt={4} width="100%" height="100%">
            <YBTable
              data={telemetryProviderTableData || []}
              options={{
                onRowClick: handleRowClick
              }}
              hover
              pagination
            >
              <TableHeaderColumn
                width="250"
                dataField="name"
                isKey
                dataSort
                dataFormat={(cell) => <span>{cell}</span>}
              >
                <span>{t('exportName')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn
                width="100"
                dataField="type"
                dataSort
                dataFormat={(cell) => <span>{TP_FRIENDLY_NAMES[cell]}</span>}
              >
                <span>{t('exportTo')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn dataFormat={formatLogExporterUsage} width="200">
                {t('logsExportUsage')}
              </TableHeaderColumn>
              <TableHeaderColumn dataFormat={formatMetricsExporterUsage} width="200">
                {t('metricsExportUsage')}
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
                {t('createExport')}
              </YBButton>
            </Box>
            <Typography variant="body2">{t('emptyMsg1')}</Typography>
            <br /> <br />
            <Typography variant="body2">
              <Trans>
                {t('learnExport')}
                <Link
                  target="_blank"
                  underline="always"
                  href="https://docs.yugabyte.com/preview/secure/audit-logging/audit-logging-ysql/"
                ></Link>
              </Trans>
            </Typography>
          </Box>
          <Box>
            <Typography variant="body2">
              <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.emptyExportNote`} />
            </Typography>
          </Box>
        </Box>
      )}
      {openExportModal && (
        <CreateTelemetryProviderConfigSidePanel
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
        <DeleteTelemetryProviderConfigModal
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
