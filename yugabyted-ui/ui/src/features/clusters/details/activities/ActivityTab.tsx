import React, { FC, useMemo } from 'react';
import { Box, Grid, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBTable, YBLoadingBox, YBModal, YBProgress, YBCodeBlock } from '@app/components';
import { BadgeVariant, YBBadge } from '@app/components/YBBadge/YBBadge';
import ArrowRightIcon from '@app/assets/caret-right-circle.svg';

const useStyles = makeStyles((theme) => ({
  arrowComponent: {
    textAlign: 'end',
    '& svg': {
      marginTop: theme.spacing(0.25),
    }
  },
  activityDetailBox: {
    padding: theme.spacing(2),
    marginTop: theme.spacing(1),
    marginBottom: theme.spacing(1.5),
    border: "1px solid #E9EEF2",
    borderRadius: theme.shape.borderRadius,
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.5),
    textTransform: 'uppercase',
    textAlign: 'start'
  },
  value: {
    paddingBottom: theme.spacing(1),
    textAlign: 'start'
  },
  pre: {
    lineHeight: 1.8,
  },
  code: {
    overflowY: "auto",
    maxHeight: "480px",
  }
}));

const dummyCode =
  `{
  "widget": {
    "debug": "on",
    "window": {
      "title": "Sample Konfabulator Widget",
      "name": "main_window",
      "width": 500,
      "height": 500
    },
    "image": {
      "src": "Images/Sun.png",
      "name": "sun1",
      "hOffset": 250,
      "vOffset": 250,
      "alignment": "center"
    },
    "text": {
      "data": "Click Here",
      "size": 36,
      "style": "bold",
      "name": "text1",
      "hOffset": 250,
      "vOffset": 100,
      "alignment": "center",
      "onMouseUp": "sun1.opacity = (sun1.opacity / 100) * 90;"
    }
  }
}`;

const StatusComponent = () => (status: BadgeVariant) => {
  return (
    <Box>
      <YBBadge variant={status} />
    </Box>
  );
}

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
  );
}

export const ActivityTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme()

  const [drawerOpenIndex, setDrawerOpen] = React.useState<number>();

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
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
      }
    },
    {
      name: 'status',
      label: t('clusterDetail.activity.status'),
      options: {
        customBodyRender: StatusComponent(),
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
      }
    },
    {
      name: 'starttime',
      label: t('clusterDetail.activity.starttime'),
      options: {
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
      }
    },
    {
      name: 'endtime',
      label: t('clusterDetail.activity.endtime'),
      options: {
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
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
      {activityData.length ?
        <Box pb={4} pt={1}>
          <YBTable
            data={activityData}
            columns={activityColumns}
            options={{ pagination: false, rowHover: true, onRowClick: (_, { dataIndex }) => setDrawerOpen(dataIndex) }}
            touchBorder={false}
          />
          <YBModal
            open={drawerOpenIndex !== undefined}
            title={t('clusterDetail.activity.details.title')}
            onClose={() => setDrawerOpen(undefined)}
            enableBackdropDismiss
            titleSeparator
            cancelLabel={t('common.cancel')}
            isSidePanel
          >
            {drawerOpenIndex !== undefined &&
              <>
                <Box className={classes.activityDetailBox}>
                  <Grid container spacing={2}>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.activity.details.operationName')}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        {activityData[drawerOpenIndex].name}
                      </Typography>
                    </Grid>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.activity.details.status')}
                      </Typography>
                      <YBBadge variant={activityData[drawerOpenIndex].status} />
                    </Grid>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.activity.details.jobID')}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        13048203-34
                      </Typography>
                    </Grid>
                    <Grid xs={6} item></Grid>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.activity.details.startTime')}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        {activityData[drawerOpenIndex].starttime}
                      </Typography>
                    </Grid>
                    <Grid xs={6} item></Grid>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.activity.details.elapsedTime')}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        3 min 15 sec
                      </Typography>
                    </Grid>
                    <Grid xs={6} item></Grid>
                    <Grid xs={12} item>
                      <Box display="flex" justifyContent="space-between">
                        <Typography variant="subtitle2" className={classes.label}>
                          {t('clusterDetail.activity.details.progress')}
                        </Typography>
                        22%
                      </Box>
                      <YBProgress value={22} color={theme.palette.primary[500]} />
                    </Grid>
                  </Grid>
                </Box>
                <YBCodeBlock text={dummyCode} blockClassName={classes.code}
                  preClassName={classes.pre} lineClassName={classes.pre} showLineNumbers />
              </>
            }
          </YBModal>
        </Box>
        : <YBLoadingBox>{t('clusterDetail.activity.noactivity')}</YBLoadingBox>
      }
    </Box>
  )
};
