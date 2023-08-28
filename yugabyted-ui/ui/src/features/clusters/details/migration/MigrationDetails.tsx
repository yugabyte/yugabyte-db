import React, { FC } from "react";
import { Box, Divider, makeStyles, Paper, Typography, useTheme } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { MigrationTiles } from "./MigrationTiles";
import { MigrationStep } from "./MigrationStep";

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

  const [selectedStep, setSelectedStep] = React.useState<number>(migration.current_step);
  React.useEffect(() => {
    setSelectedStep(
      migration.migration_phase === 0
        ? 1
        : migration.migration_phase === 1
        ? 0
        : migration.migration_phase <= 4
        ? 2
        : 3
    );
  }, [migration]);

  return (
    <Box mt={1}>
      <Paper>
        <Box p={4}>
          <Typography variant="h4" className={classes.heading}>
            {migration.migration_name}
          </Typography>
          <Box display="flex" gridGap={theme.spacing(5)}>
            <Box width={300} flexShrink={0}>
              <MigrationTiles
                steps={steps}
                currentStep={selectedStep}
                onStepChange={setSelectedStep}
                phase={migration.migration_phase}
              />
            </Box>
            <Box>
              <Divider orientation="vertical" />
            </Box>
            <Box flex={1}>
              <MigrationStep steps={steps} migration={migration} step={selectedStep} />
            </Box>
          </Box>
        </Box>
      </Paper>
    </Box>
  );
};
