import React, { FC } from "react";
import { Box, Divider } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { MigrationTiles } from "./MigrationTiles";
import { MigrationStep } from "./MigrationStep";

interface MigrationDetailsProps {
  steps: string[];
  migration: Migration;
  onRefetch: () => void;
  isFetching?: boolean;
}

export const MigrationDetails: FC<MigrationDetailsProps> = ({
  steps = [""],
  migration,
  onRefetch,
  isFetching = false,
}) => {
  const [selectedStep, setSelectedStep] = React.useState<number>(migration.landing_step);
  React.useEffect(() => {
    setSelectedStep(migration.landing_step);
  }, [migration.landing_step]);

  return (
    <Box ml={-2} pt={2}>
      <Divider orientation="horizontal" />
      <Box display="flex">
        <Box width={300} flexShrink={0}>
          <MigrationTiles
            steps={steps}
            currentStep={selectedStep}
            onStepChange={setSelectedStep}
            phase={migration.migration_phase}
            migration={migration}
            isFetching={isFetching}
          />
        </Box>
        <Box mr={2}>
          <Divider orientation="vertical" />
        </Box>
        <Box flex={1} mt={3} minWidth={0}>
          <MigrationStep
            steps={steps}
            migration={migration}
            step={selectedStep}
            onRefetch={onRefetch}
            isFetching={isFetching}
          />
        </Box>
      </Box>
    </Box>
  );
};
