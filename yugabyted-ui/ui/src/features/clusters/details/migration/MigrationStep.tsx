import React, { FC } from "react";
import { Box } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { MigrationData } from "./steps/MigrationData";
import { MigrationAssessment } from "./steps/assessment/MigrationAssessment";
import { MigrationSchema } from "./steps/schema/NewMigrationSchema";
import { MigrationVerify } from "./steps/MigrationVerify";
import {
  useGetAssessmentSourceDBInfoQuery,
  useGetAssessmentTargetRecommendationInfoQuery,
  useGetMigrationAssessmentInfoQuery,
  useGetVoyagerDataMigrationMetricsQuery,
  useGetVoyagerMigrateSchemaTasksQuery,
  useGetVoyagerMigrationAssesmentDetailsQuery,
} from "@app/api/src";

interface MigrationStepProps {
  steps: string[];
  migration: Migration;
  step: number;
  onRefetch: () => void;
  isFetching?: boolean;
}

const stepComponents = [MigrationAssessment, MigrationSchema, MigrationData, MigrationVerify];

export const MigrationStep: FC<MigrationStepProps> = ({
  steps = [""],
  migration,
  step,
  onRefetch,
  isFetching = false,
}) => {
  const { refetch: refetchMigrationAssesmentDetails } = useGetVoyagerMigrationAssesmentDetailsQuery(
    {
      uuid: migration.migration_uuid || "migration_uuid_not_found",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchMigrationAssesmentInfo } = useGetMigrationAssessmentInfoQuery(
    {
      uuid: migration.migration_uuid || "migration_uuid_not_found",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchMigrationAssesmentSourceDB } = useGetAssessmentSourceDBInfoQuery(
    {
      uuid: migration.migration_uuid || "migration_uuid_not_found",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchTargetRecommendation } = useGetAssessmentTargetRecommendationInfoQuery(
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
    refetchMigrationAssesmentInfo();
    refetchMigrationAssesmentSourceDB();
    refetchTargetRecommendation();
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
              isFetching={isFetching}
            />
          );
        }
        return <React.Fragment key={index} />;
      })}
    </Box>
  );
};
