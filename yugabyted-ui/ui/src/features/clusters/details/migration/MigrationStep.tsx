import React, { FC } from "react";
import { Box } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { MigrationData } from "./steps/MigrationData";
import { MigrationAssessment } from "./steps/assessment/MigrationAssessment";
import { MigrationSchema } from "./steps/MigrationSchema";
import { MigrationVerify } from "./steps/MigrationVerify";
import {
  useGetVoyagerDataMigrationMetricsQuery,
  useGetVoyagerMigrateSchemaTasksQuery,
  useGetVoyagerMigrationAssesmentDetailsQuery,
} from "@app/api/src";

interface MigrationStepProps {
  steps: string[];
  migration: Migration;
  step: number;
  onRefetch: () => void;
  onStepChange?: (step: number) => void;
  isFetching?: boolean;
}

const stepComponents = [MigrationAssessment, MigrationSchema, MigrationData, MigrationVerify];

export const MigrationStep: FC<MigrationStepProps> = ({
  steps = [""],
  migration,
  step,
  onRefetch,
  onStepChange,
  isFetching = false,
}) => {
  const { refetch: refetchMigrationAssesmentDetails } = useGetVoyagerMigrationAssesmentDetailsQuery(
    {
      uuid: migration.migration_uuid || "migration_uuid_not_found",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchMigrationSchemaTasks } = useGetVoyagerMigrateSchemaTasksQuery(
    {
      uuid: migration.migration_uuid || "migration_uuid_not_found",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchMigrationMetrics } = useGetVoyagerDataMigrationMetricsQuery(
    {
      uuid: migration.migration_uuid || "migration_uuid_not_found",
    },
    { query: { enabled: false } }
  );

  const refetch = React.useCallback(() => {
    // Refetch all migration apis to avoid inconsistent states
    onRefetch();
    refetchMigrationAssesmentDetails();
    refetchMigrationSchemaTasks();
    refetchMigrationMetrics();
  }, []);

  return (
    <Box mt={1}>
      {stepComponents.map((StepComponent, index) => {
        if (index === step) {
          return (
            <StepComponent
              key={index}
              step={index}
              heading={steps[step]}
              migration={migration}
              onRefetch={refetch}
              onStepChange={onStepChange}
              isFetching={isFetching}
            />
          );
        }
        return <React.Fragment key={index} />;
      })}
    </Box>
  );
};
