import React, { FC, useMemo } from "react";
import { Box, Divider, Grid, makeStyles, Paper, Typography } from "@material-ui/core";
import { ProgressStepper } from "../ProgressStepper";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { YBTable } from "@app/components";

const useStyles = makeStyles((theme) => ({
  arrowComponent: {
    textAlign: "end",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "start",
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(1.5),
    marginBottom: theme.spacing(1.5),
  },
  heading: {
    marginBottom: theme.spacing(5),
  },
}));

const StatusComponent = () => (status: BadgeVariant) => {
  return (
    <Box>
      <YBBadge variant={status} />
    </Box>
  );
};

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
  );
};

interface MigrationOverviewProps {}

export const MigrationOverview: FC<MigrationOverviewProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const migrationData = useMemo(
    () => [
      {
        name: "Migration ABC",
        status: BadgeVariant.InProgress,
        starttime: "11/07/2022, 09:55",
        endtime: "-",
      },
      {
        name: "Migration XYZ",
        status: BadgeVariant.InProgress,
        starttime: "11/01/2022, 09:52",
        endtime: "-",
      },
    ],
    []
  );

  const migrationColumns = [
    {
      name: "name",
      label: t("clusterDetail.voyager.name"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "status",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: StatusComponent(),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "starttime",
      label: t("clusterDetail.voyager.starttime"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "endtime",
      label: t("clusterDetail.voyager.endtime"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "",
      label: "",
      options: {
        sort: false,
        customBodyRender: ArrowComponent(classes),
      },
    },
  ];

  const analyzeColumns = [
    {
      name: 'name',
      label: t('clusterDetail.voyager.name'),
      options: {
        setCellProps: () => ({ style: { padding: '8px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px' } }),
      }
    },
    {
      name: 'status',
      label: t('clusterDetail.voyager.status'),
      options: {
        setCellProps: () => ({ style: { padding: '8px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px' } }),
      }
    },
    {
      name: 'schema',
      label: 'Schema',
      options: {
        setCellProps: () => ({ style: { padding: '8px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px' } }),
      }
    },
  ];

  return (
    <Box display="flex" flexDirection="column" gridGap={10}>
      <YBTable
        data={migrationData}
        columns={migrationColumns}
        options={{ pagination: false, rowHover: true, onRowClick: (_, { dataIndex }) => {} }}
        touchBorder={false}
      />

      <Box mt={2}>
        <Paper>
          <ProgressStepper />
        </Paper>
      </Box>
      <Box mt={2}>
        <Paper>
          <Box p={4}>
            <Typography variant="h4" className={classes.heading}>
              {t("clusterDetail.voyager.migrationOverview")}
            </Typography>
            <Grid container spacing={4}>
              {new Array(4).fill(0).map((_, index) => (
                <Grid key={index} item xs={3}>
                  <Typography variant="subtitle2" className={classes.label}>
                    Data
                  </Typography>
                  <Typography variant="body2" className={classes.value}>
                    Value
                  </Typography>
                </Grid>
              ))}
              <Divider orientation="horizontal" className={classes.dividerHorizontal} />
              {new Array(4).fill(0).map((_, index) => (
                <Grid key={index} item xs={3}>
                  <Typography variant="subtitle2" className={classes.label}>
                    Data
                  </Typography>
                  <Typography variant="body2" className={classes.value}>
                    Value
                  </Typography>
                </Grid>
              ))}
            </Grid>
          </Box>
        </Paper>
      </Box>

      <Box mt={2}>
        <Paper>
          <Box p={4}>
            <Typography variant="h4" className={classes.heading}>
              {t("clusterDetail.voyager.analyze")}
            </Typography>
            <YBTable
              data={[]}
              columns={analyzeColumns}
              options={{ pagination: true }}
              withBorder={false}
            />
          </Box>
        </Paper>
      </Box>
    </Box>
  );
};
