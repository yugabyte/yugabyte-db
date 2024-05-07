import React, { FC } from "react";
import { Box, Divider, Grid, Paper, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton, YBTooltip } from "@app/components";
import CaretRightIcon from "@app/assets/Drilldown.svg";
import { MigrationSourceObjects } from "./AssessmentSourceObjects";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "left",
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  pointer: {
    cursor: "pointer",
  },
}));

interface MigrationSourceEnvProps {
  vcpu: string;
  memory: string;
  disk: string;
  connectionCount: string;
  tableSize: string;
  indexSize: string;
  totalSize: string;
  rowCount: string;
}

export const MigrationSourceEnv: FC<MigrationSourceEnvProps> = ({
  vcpu,
  memory,
  disk,
  connectionCount,
  tableSize,
  indexSize,
  totalSize,
  rowCount,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const [showSourceObjects, setShowSourceObjects] = React.useState<boolean>(false);

  return (
    <Paper>
      <Box px={2} py={3}>
        <Box
          display="flex"
          justifyContent="space-between"
          alignItems="center"
          className={classes.heading}
        >
          <Box display="flex" alignItems="center" gridGap={theme.spacing(0.6)}>
            <Typography variant="h4">
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.heading")}
            </Typography>
            <Box>
              <YBTooltip title={t("clusterDetail.voyager.planAndAssess.sourceEnv.tooltip")} />
            </Box>
          </Box>

          <YBButton
            variant="ghost"
            startIcon={<CaretRightIcon />}
            onClick={() => setShowSourceObjects(true)}
          >
            {t("clusterDetail.voyager.planAndAssess.sourceEnv.viewDetails")}
          </YBButton>
        </Box>

        <Grid container spacing={4}>
          <Grid item xs={3}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.totalVcpu")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {vcpu}
            </Typography>
          </Grid>
          <Grid item xs={3}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.totalMemory")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {memory}
            </Typography>
          </Grid>
          <Grid item xs={3}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.totalDisk")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {disk}
            </Typography>
          </Grid>
          <Grid item xs={3}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.noOfConns")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {connectionCount}
            </Typography>
          </Grid>
        </Grid>
        <Divider orientation="horizontal" className={classes.dividerHorizontal} />
        <Box mb={2}>
          <Typography variant="body2">
            {t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceDB")}
          </Typography>
        </Box>
        <Grid container spacing={4}>
          <Grid item xs={3}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.tableSize")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {tableSize}
            </Typography>
          </Grid>
          <Grid item xs={3}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.indexSize")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {indexSize}
            </Typography>
          </Grid>
          <Grid item xs={3}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.totalSize")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {totalSize}
            </Typography>
          </Grid>
          <Grid item xs={3}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.rowCount")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {rowCount}
            </Typography>
          </Grid>
        </Grid>
      </Box>

      <MigrationSourceObjects
        open={showSourceObjects}
        onClose={() => setShowSourceObjects(false)}
      />
    </Paper>
  );
};
