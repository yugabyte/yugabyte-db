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
  onRefetch: () => void;
}

export const MigrationDetails: FC<MigrationDetailsProps> = ({
  steps = [""],
  migration,
  onRefetch,
}) => {
  const theme = useTheme();
  const classes = useStyles();

  const [selectedStep, setSelectedStep] = React.useState<number>(migration.landing_step);
  React.useEffect(() => {
    setSelectedStep(migration.landing_step);
  }, [migration.landing_step]);

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
            <Box flex={1} minWidth={0}>
              <MigrationStep
                steps={steps}
                migration={migration}
                step={selectedStep}
                onRefetch={onRefetch}
              />
            </Box>
          </Box>
        </Box>
      </Paper>
    </Box>
  );
};
