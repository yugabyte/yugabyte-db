import React, { FC } from "react";
import { Box, makeStyles, Paper, Typography } from "@material-ui/core";
import type { Migration } from "../MigrationOverview";
import { STATUS_TYPES, YBStatus } from "@app/components";
import { useTranslation } from "react-i18next";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(5),
  },
}));

interface MigrationSchemaProps {
  heading: string;
  migration: Migration;
  phase: number;
}

export const MigrationSchema: FC<MigrationSchemaProps> = ({ heading, migration, phase }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const isRunning = migration.migration_phase === phase;

  return (
    <Paper>
      <Box p={4}>
        <Typography variant="h4" className={classes.heading}>
          {heading}
        </Typography>
        <Box display="flex" gridGap={4} alignItems="center">
          <YBStatus type={isRunning ? STATUS_TYPES.IN_PROGRESS : STATUS_TYPES.SUCCESS} size={42} />
          <Box display="flex" flexDirection="column">
            <Typography variant="h5">
              {isRunning
                ? t("clusterDetail.voyager.migratingSchema")
                : t("clusterDetail.voyager.migratedSchema")}
            </Typography>
            <Typography variant="body2">
              {isRunning
                ? t("clusterDetail.voyager.migratingSchemaDesc")
                : t("clusterDetail.voyager.migratedSchemaDesc")}
            </Typography>
          </Box>
        </Box>
      </Box>
    </Paper>
  );
};
