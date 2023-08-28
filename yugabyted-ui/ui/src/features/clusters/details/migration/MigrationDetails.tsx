import React, { FC } from "react";
import { Box, Divider, makeStyles, Paper, Typography, useTheme } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { MigrationTiles } from "./MigrationTiles";
import { MigrationPhase } from "./MigrationPhase";

const useStyles = makeStyles((theme) => ({
  heading: {
    margin: theme.spacing(0, 0, 3.5, 0),
  },
}));

interface MigrationDetailsProps {
  steps: string[];
  migration: Migration;
}

export const MigrationDetails: FC<MigrationDetailsProps> = ({ steps = [""], migration }) => {
  const theme = useTheme();
  const classes = useStyles();

  const [selectedPhase, setSelectedPhase] = React.useState<number>(migration.migration_phase);
  React.useEffect(() => {
    setSelectedPhase(migration.migration_phase);
  }, [migration.migration_phase]);

  return (
    <Box mt={1}>
      <Paper>
        <Box p={4}>
          <Typography variant="h4" className={classes.heading}>
            {migration.migration_name}
          </Typography>
          <Box display="flex" gridGap={theme.spacing(5)}>
            <Box width={300}>
              <MigrationTiles
                steps={steps}
                currentStep={selectedPhase}
                onStepChange={setSelectedPhase}
                runningStep={migration.migration_phase}
              />
            </Box>
            <Box>
              <Divider orientation="vertical" />
            </Box>
            <Box flex={1}>
              <MigrationPhase steps={steps} migration={migration} phase={selectedPhase} />
            </Box>
          </Box>
        </Box>
      </Paper>
    </Box>
  );
};
