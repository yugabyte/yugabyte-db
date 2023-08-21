import React, { FC } from "react";
import { Box, makeStyles, Paper, Typography } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { MigrationTiles } from "./MigrationTiles";

const useStyles = makeStyles((theme) => ({
  heading: {
    margin: theme.spacing(0, 0, 2, 0),
  },
}));

interface MigrationDataProps {
  steps: string[];
  migration: Migration;
  onSelectPhase: (index: number) => void;
}

export const MigrationData: FC<MigrationDataProps> = ({
  steps = [""],
  migration,
  onSelectPhase,
}) => {
  const classes = useStyles();

  return (
    <Box mt={1}>
      <Paper>
        <Box p={4}>
          <Typography variant="h4" className={classes.heading}>
            {migration.migration_name}
          </Typography>
          <MigrationTiles
            steps={steps}
            onStepChange={onSelectPhase}
            runningStep={migration.migration_phase}
          />
        </Box>
      </Paper>
    </Box>
  );
};
