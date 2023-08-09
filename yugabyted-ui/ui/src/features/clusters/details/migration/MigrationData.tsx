import React, { FC } from "react";
import { Box, makeStyles, Paper, Typography } from "@material-ui/core";
import { ProgressStepper } from "../ProgressStepper";
import type { Migration } from "./MigrationOverview";
import { MigrationAnalyze } from "./MigrationAnalyze";
import { MigrationExportImport } from "./MigrationExportImport";

const useStyles = makeStyles((theme) => ({
  heading: {
    margin: theme.spacing(0, 0, 2, 0),
  },
}));

interface MigrationDataProps {
  steps: string[];
  migration: Migration;
}

export const MigrationData: FC<MigrationDataProps> = ({ steps = [""], migration }) => {
  const classes = useStyles();

  const [step, setStep] = React.useState<(typeof migration)["step"]>(migration.step);

  React.useEffect(() => {
    setStep(migration.step);
  }, [migration.step]);

  return (
    <Box mt={2}>
      <Paper>
        <Box p={4}>
          <Typography variant="h4" className={classes.heading}>
            {migration.name}
          </Typography>
          <ProgressStepper
            steps={steps}
            step={step}
            onStepChange={setStep}
            runningStep={migration.step}
          />
        </Box>
      </Paper>

      <Box mt={2}>
        {step === 0 ? (
          <MigrationAnalyze heading={steps[step]} migration={migration} />
        ) : (
          <MigrationExportImport heading={steps[step]} migration={migration} />
        )}
      </Box>
    </Box>
  );
};
