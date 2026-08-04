import React, { FC } from "react";
import { Box, Divider, Paper, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { MigrationSourceEnvSidePanel } from "./AssessmentSourceEnvSidePanel";
import type { Migration } from "../../MigrationOverview";
import { YBButton } from "@app/components";
import CaretRightIconBlue from "@app/assets/caretRightIconBlue.svg";
import { MetadataItem } from "../../components/MetadataItem";
import { DatabaseTypeDisplay } from "../../components/DatabaseTypeDisplay";

const useStyles = makeStyles((theme) => ({
  heading: {
    display: "flex",
    justifyContent: "space-between",
    alignItems: "center",
    padding: 24,
    flex: "0 0 auto"
  },
  paper: {
    overflow: "clip",
    border: "1px solid #E9EEF2",
    borderRadius: theme.shape.borderRadius,
    height: "100%",
    display: "flex",
    flexDirection: "column"
  },
  boxBody: {
    display: "flex",
    flexDirection: "column",
    gridGap: theme.spacing(2),
    backgroundColor: theme.palette.info[400],
    padding: 24,
    flex: 1,
    height: "100%"
  },
  schemaButton: {
    marginLeft: 'auto'
  },
  schemaRow: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    width: '100%'
  }
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
      <Box className={classes.heading}>
        <Typography variant="h5">
          {t("clusterDetail.voyager.planAndAssess.sourceEnv.heading")}
        </Typography>
      </Box>

      <Divider orientation="horizontal" />

      <Box className={classes.boxBody}>
        <MetadataItem
          layout="horizontal"
          label={[
            t("clusterDetail.voyager.planAndAssess.sourceEnv.databaseType"),
            t("clusterDetail.voyager.planAndAssess.sourceEnv.hostname"),
            t("clusterDetail.voyager.planAndAssess.sourceEnv.database"),
            t("clusterDetail.voyager.planAndAssess.sourceEnv.schema")
          ]}
          value={[
            migration?.source_db?.engine
              ? <DatabaseTypeDisplay type={migration.source_db.engine} /> : "N/A",
            migration?.source_db?.ip ?? "N/A",
            migration?.source_db?.database ?? "N/A",
            migration?.source_db?.schema?.replaceAll('|', ', ') ?? "N/A"
          ]}
        />

        <YBButton
          variant="ghost"
          startIcon={<CaretRightIconBlue />}
          onClick={() => setShowSourceObjects(true)}
          className={classes.schemaButton}
        >
          {t("clusterDetail.voyager.planAndAssess.sourceEnv.schemaDetails")}
        </YBButton>
      </Box>

      <MigrationSourceEnvSidePanel
        migration={migration}
        open={showSourceObjects}
        onClose={() => setShowSourceObjects(false)}
      />
    </Paper>
  );
};
