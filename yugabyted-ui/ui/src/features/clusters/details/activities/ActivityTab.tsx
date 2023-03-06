import React, { FC, useMemo } from 'react';
import { Box, makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBTable, YBLoadingBox } from '@app/components';
import type { ClassNameMap } from '@material-ui/styles';
import { BadgeVariant, YBBadge } from '@app/components/YBBadge/YBBadge';
import ArrowRightIcon from '@app/assets/Drilldown.svg';

const useStyles = makeStyles((theme) => ({
    statusComponent: {
        padding: '8px 0',
        width: 'fit-content'
    },
    arrowComponent: {
        padding: '8px 0',
        textAlign: 'end',
        '& svg': {
            paddingTop: theme.spacing(0.25),
        }
    },
  }));

const StatusComponent = (classes: ClassNameMap) => (status: BadgeVariant) => {
    return (
        <Box className={classes.statusComponent}>
            <YBBadge variant={status} />
        </Box>
    );
}

const ArrowComponent = (classes: ClassNameMap) => () => {
    return (
        <Box className={classes.arrowComponent}>
            <ArrowRightIcon />
        </Box>
    );
}

export const ActivityTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const activityData = useMemo(() => [
        {
            name: "Index backfill",
            status: BadgeVariant.InProgress,
            starttime: "11/07/2022, 09:55",
            endtime: "-"
        },
        {
            name: "Edit cluster",
            status: BadgeVariant.InProgress,
            starttime: "11/01/2022, 09:52",
            endtime: "-"
        },
    ], []);

  const activityColumns = [
    {
        name: 'name',
        label: t('clusterDetail.activity.activity'),
        options: {
            setCellHeaderProps: () => ({ style: { padding: '8px 16px' }}),
        }
    },
    {
        name: 'status',
        label: t('clusterDetail.activity.status'),
        options: {
            customBodyRender: StatusComponent(classes),
            setCellHeaderProps: () => ({ style: { padding: '8px 16px' }}),
        }
    },
    {
        name: 'starttime',
        label: t('clusterDetail.activity.starttime'),
        options: {
            setCellHeaderProps: () => ({ style: { padding: '8px 16px' }}),
        }
    },
    {
        name: 'endtime',
        label: t('clusterDetail.activity.endtime'),
        options: {
            setCellHeaderProps: () => ({ style: { padding: '8px 16px' }}),
        }
    },
    {
        name: '',
        label: '',
        options: {
            sort: false,
            customBodyRender: ArrowComponent(classes),
        }
    },
  ];

  return (
    <Box>
      { activityData.length ?
        <Box pb={4} pt={1}>
            <YBTable
                data={activityData}
                columns={activityColumns}
                options={{ pagination: false, rowHover: true }}
            />
        </Box>
        : <YBLoadingBox>{t('clusterDetail.activity.noactivity')}</YBLoadingBox>
      }
    </Box>
  )
};
