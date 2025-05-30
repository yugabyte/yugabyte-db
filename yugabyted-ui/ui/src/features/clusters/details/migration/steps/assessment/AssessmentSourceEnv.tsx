import React, { FC } from "react";
import { Box, Divider, Grid, Paper, Typography, makeStyles} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { MigrationSourceEnvSidePanel } from "./AssessmentSourceEnvSidePanel";
import type { Migration } from "../../MigrationOverview";
import { YBButton } from "@app/components";
import CaretRightIcon from "@app/assets/caret-right-circle.svg";

const useStyles = makeStyles((theme) => ({
  heading: {
    display: "flex",
    justifyContent: "space-between",
    alignItems: "center",
    paddingTop: theme.spacing(3),
    paddingBottom: theme.spacing(3),
    paddingRight: theme.spacing(3),
    paddingLeft: theme.spacing(3),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left",
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  value: {
    textAlign: "start",
  },
  pointer: {
    cursor: "pointer",
  },
  dividerVertical: {
    marginLeft: theme.spacing(2.5),
    marginRight: theme.spacing(2.5),
  },
  paper: {
    overflow: "clip",
    height: "100%",
  },
  boxBody: {
    display: "flex",
    flexDirection: "column",
    gridGap: theme.spacing(2),
    backgroundColor: theme.palette.info[400],
    height: "100%",
    paddingRight: theme.spacing(3),
    paddingLeft: theme.spacing(3),
    paddingTop: theme.spacing(3),
    paddingBottom: theme.spacing(3),
  },
  schemaButton: {
    float: "right",
  },
}));

interface MigrationSourceEnvProps {
  migration: Migration | undefined;
}

export const MigrationSourceEnv: FC<MigrationSourceEnvProps> = ({
  migration,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [showSourceObjects, setShowSourceObjects] = React.useState<boolean>(false);

  return (
    <Paper className={classes.paper}>
      <Box height="100%">
        <Box className={classes.heading}>
          <Typography variant="h5">
            {t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceDB")}
          </Typography>
        </Box>
        <Divider orientation="horizontal" />

        <Box className={classes.boxBody}>
          <Grid container spacing={2}>
            <Grid item xs={3}>
              <Typography variant="body2" className={classes.label}>
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.databaseType")}
              </Typography>
            </Grid>
            <Grid item xs={9}>
              <Typography variant="body2" className={classes.value}>
                {migration?.source_db?.engine ?? "N/A"}
              </Typography>
            </Grid>
            <Grid item xs={3}>
              <Typography variant="body2" className={classes.label}>
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.hostname")}
              </Typography>
            </Grid>
            <Grid item xs={9}>
              <Typography variant="body2" className={classes.value}>
                {migration?.source_db?.ip ?? "N/A"}
              </Typography>
            </Grid>
            <Grid item xs={3}>
              <Typography variant="body2" className={classes.label}>
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.database")}
              </Typography>
            </Grid>
            <Grid item xs={9}>
              <Typography variant="body2" className={classes.value}>
                {migration?.source_db?.database ?? "N/A"}
              </Typography>
            </Grid>
            <Grid item xs={3}>
              <Typography variant="body2" className={classes.label}>
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.schema")}
              </Typography>
            </Grid>
            <Grid item xs={6}>
              <Typography variant="body2" className={classes.value}>
                {migration?.source_db?.schema?.replaceAll('|', ', ') ?? "N/A"}
              </Typography>
            </Grid>
            <Grid item xs={3}>
              <YBButton
                variant="ghost"
                startIcon={<CaretRightIcon />}
                onClick={() => setShowSourceObjects(true)}
                className={classes.schemaButton}
              >
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.schemaDetails")}
              </YBButton>
            </Grid>
          </Grid>
        </Box>
      </Box>

      <MigrationSourceEnvSidePanel
        migration={migration}
        open={showSourceObjects}
        onClose={() => setShowSourceObjects(false)}
      />
    </Paper>
  );
};
