import React, { FC } from 'react';
import { useTranslation, Namespace, TFunction } from 'react-i18next';
import { useRouteMatch } from 'react-router-dom';
import { makeStyles, Box } from '@material-ui/core';
import type { ClassNameMap } from '@material-ui/styles';
import type { MUIDataTableMeta } from 'mui-datatables';
import { intlFormat } from 'date-fns';

import { YBButton, YBTable, STATUS_TYPES, YBStatus } from '@app/components';
import { useListTasksQuery, TaskTypeEnum, ListTasksEntityTypeEnum } from '@app/api/src';
import CaretRightIcon from '@app/assets/caret-right.svg';

const useStyles = makeStyles((theme) => ({
  nextSection: {
    width: theme.spacing(3),
    height: theme.spacing(3),
    borderRadius: '50%',
    minWidth: 'unset',
    padding: 0
  }
}));

const convertToLocale = (timeStr: string) => {
  if (timeStr) {
    return intlFormat(Date.parse(timeStr), {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      // @ts-ignore: Parameter is not yet supported by `date-fns` but
      // is supported by underlying Intl.DateTimeFormat. CLOUDGA-5283
      hourCycle: 'h23'
    });
  }
  return String.fromCharCode(8211); // Use en-dash for all scenarios that there is no value
};

const getTableActionsComponent = (classes: ClassNameMap) => {
  const TableActionsComponent = (_value: unknown, columnMeta: MUIDataTableMeta) => {
    if (columnMeta.rowData[3] === 'Failed') {
      return (
        <YBButton size="small" variant="secondary" className={classes.nextSection}>
          <CaretRightIcon />
        </YBButton>
      );
    }
    return <span />;
  };

  return TableActionsComponent;
};

const getTableStatusComponent = (translation: TFunction<Namespace>) => {
  const TableStatusComponent = (value: unknown) => {
    // AC: State does not have an enum associated with it
    if (value === 'FAILED') {
      return (
        <Box mr="auto" width="fit-content">
          <YBStatus type={STATUS_TYPES.FAILED} label={translation('clusterDetail.activity.statusEnum.failed')} />
        </Box>
      );
    }
    if (value === 'SUCCEEDED') {
      return (
        <Box mr="auto" width="fit-content">
          <YBStatus type={STATUS_TYPES.SUCCESS} label={translation('clusterDetail.activity.statusEnum.completed')} />
        </Box>
      );
    }
    return (
      <Box mr="auto" width="fit-content">
        <YBStatus type={STATUS_TYPES.IN_PROGRESS} label={translation('clusterDetail.activity.statusEnum.inProgress')} />
      </Box>
    );
  };
  return TableStatusComponent;
};

export const ActivityTab: FC = () => {
  const { params } = useRouteMatch<App.RouteParams>();
  const { data: tasksData } = useListTasksQuery({
    accountId: params.accountId,
    projectId: params.projectId,
    entity_id: params.clusterId,
    entity_type: ListTasksEntityTypeEnum.Cluster
  });
  const { t } = useTranslation();
  const classes = useStyles();

  const activityColumns = [
    {
      name: 'id',
      options: {
        display: false,
        filter: false
      }
    },
    {
      name: 'task_type',
      label: 'Type',
      options: {
        filter: true,
        customBodyRender: (value: string) => {
          // AC: State does not have an enum associated with it
          if (value === TaskTypeEnum.CreateCluster) {
            return t('clusterDetail.activity.createCluster');
          }
          if (value === TaskTypeEnum.CreateBackup) {
            return t('clusterDetail.activity.createBackup');
          }
          if (value === TaskTypeEnum.EditCluster) {
            return t('clusterDetail.activity.editCluster');
          }
          if (value === TaskTypeEnum.RestoreBackup) {
            return t('clusterDetail.activity.restoreBackup');
          }
          if (value === TaskTypeEnum.MigrateCluster) {
            return t('clusterDetail.activity.migrateCluster');
          }
          if (value === TaskTypeEnum.DeleteCluster) {
            return t('clusterDetail.activity.deleteCluster');
          }
          if (value === TaskTypeEnum.EditAllowList) {
            return t('clusterDetail.activity.editAllowList');
          }
          if (value === TaskTypeEnum.UpgradeCluster) {
            return t('clusterDetail.activity.upgradeCluster');
          }
          if (value === TaskTypeEnum.PauseCluster) {
            return t('clusterDetail.activity.pauseCluster');
          }
          if (value === TaskTypeEnum.ResumeCluster) {
            return t('clusterDetail.activity.resumeCluster');
          }
          // Should not reach here
          return t('clusterDetail.activity.unknown');
        }
      }
    },
    {
      name: 'state',
      label: 'Status',
      options: {
        filter: true,
        customBodyRender: getTableStatusComponent(t)
      }
    },
    {
      name: 'created_on',
      label: 'Start Time',
      options: {
        filter: true,
        customBodyRender: (time: string) => convertToLocale(time)
      }
    },
    {
      name: 'completed_on',
      label: 'End Time',
      options: {
        filter: true,
        customBodyRender: (time: string) => convertToLocale(time)
      }
    },
    {
      name: 'details',
      label: 'Details',
      options: {
        filter: true,
        hideHeader: true,
        customBodyRender: getTableActionsComponent(classes)
      }
    }
  ];

  const tableRows =
    tasksData?.data?.map((task) => ({
      ...task.info,
      created_on: task.info?.metadata?.created_on
    })) ?? [];

  return (
    <YBTable
      data={tableRows}
      columns={activityColumns}
      options={{
        pagination: true
      }}
    />
  );
};
