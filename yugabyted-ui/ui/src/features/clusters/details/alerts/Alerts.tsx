import React, { FC, useMemo } from 'react';
import { Box, Grid, makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBTable, YBLoadingBox, YBModal, YBCodeBlock } from '@app/components';
import { BadgeVariant, YBBadge } from '@app/components/YBBadge/YBBadge';
import ArrowRightIcon from '@app/assets/caret-right-circle.svg';

const useStyles = makeStyles((theme) => ({
  title: {
    marginTop: theme.spacing(2),
    marginBottom: theme.spacing(1),
  },
  arrowComponent: {
    textAlign: 'end',
    '& svg': {
      marginTop: theme.spacing(0.25),
    }
  },
  alertDetailBox: {
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

export const Alerts: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [drawerOpenIndex, setDrawerOpen] = React.useState<number>();

  const alerts = useMemo(() => [
    {
      name: "CPU usage exceeds 75% for node ABC",
      status: BadgeVariant.Warning,
      triggertime: "11/07/2022, 09:55",
    },
    {
      name: "Node XYZ crashed",
      status: BadgeVariant.Error,
      triggertime: "11/01/2022, 09:52",
    },
  ], []);

  const alertColumns = [
    {
      name: 'name',
      label: t('clusterDetail.alerts.alert'),
      options: {
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
      }
    },
    {
      name: 'status',
      label: t('clusterDetail.alerts.status'),
      options: {
        customBodyRender: StatusComponent(),
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
      }
    },
    {
      name: 'triggertime',
      label: t('clusterDetail.alerts.triggertime'),
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
      <Typography variant="h5" className={classes.title}>{t('clusterDetail.alerts.alerts')}</Typography>
      {alerts.length ?
        <Box pb={4} pt={1}>
          <YBTable
            data={alerts}
            columns={alertColumns}
            options={{ pagination: false, rowHover: true, onRowClick: (_, { dataIndex }) => setDrawerOpen(dataIndex) }}
            touchBorder={false}
          />
          <YBModal
            open={drawerOpenIndex !== undefined}
            title={t('clusterDetail.alerts.details.title')}
            onClose={() => setDrawerOpen(undefined)}
            enableBackdropDismiss
            titleSeparator
            cancelLabel={t('common.close')}
            isSidePanel
          >
            {drawerOpenIndex !== undefined &&
              <>
                <Box className={classes.alertDetailBox}>
                  <Grid container spacing={2}>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.alerts.details.name')}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        {alerts[drawerOpenIndex].name}
                      </Typography>
                    </Grid>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.alerts.details.status')}
                      </Typography>
                      <YBBadge variant={alerts[drawerOpenIndex].status} />
                    </Grid>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.alerts.details.id')}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        13048203-34
                      </Typography>
                    </Grid>
                    <Grid xs={6} item></Grid>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t('clusterDetail.alerts.details.triggerTime')}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        {alerts[drawerOpenIndex].triggertime}
                      </Typography>
                    </Grid>
                    <Grid xs={6} item></Grid>
                  </Grid>
                </Box>
                <YBCodeBlock text={dummyCode} blockClassName={classes.code}
                  preClassName={classes.pre} lineClassName={classes.pre} showLineNumbers />
              </>
            }
          </YBModal>
        </Box>
        : <YBLoadingBox>{t('clusterDetail.alerts.noalerts')}</YBLoadingBox>
      }
    </Box>
  )
};
