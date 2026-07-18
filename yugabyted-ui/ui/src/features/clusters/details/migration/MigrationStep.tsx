import React, { FC } from "react";
import { Box } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { MigrationData } from "./steps/data/NewMigrationData";
import { MigrationAssessment } from "./steps/assessment/MigrationAssessment";
import { MigrationSchema } from "./steps/schema/NewMigrationSchema";
import { MigrationVerify } from "./steps/MigrationVerify";
import {
  useGetAssessmentSourceDBInfoQuery,
  useGetAssessmentTargetRecommendationInfoQuery,
  useGetMigrationAssessmentInfoQuery,
  useGetVoyagerDataMigrationMetricsQuery,
  useGetVoyagerMigrateSchemaTasksQuery,
  MigrationAssessmentReport,
} from "@app/api/src";

interface MigrationStepProps {
  steps: string[];
  migration: Migration | undefined;
  step: number;
  onRefetch?: () => void;
  isFetching?: boolean;
  isNewMigration?: boolean;
  voyagerVersion?: string;
}

const stepComponents = [MigrationAssessment, MigrationSchema, MigrationData, MigrationVerify];

export const MigrationStep: FC<MigrationStepProps> = ({
  steps = [""],
  migration,
  step,
  isFetching = false,
  isNewMigration = false,
}) => {
  const { refetch: refetchMigrationAssesmentDetails } = useGetMigrationAssessmentInfoQuery(
    {
      uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
    },
    { query: { enabled: false } }
  );
  const { data: migrationAssessmentData } = useGetMigrationAssessmentInfoQuery({
    uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
  });
  const mAssessmentData = migrationAssessmentData as MigrationAssessmentReport;
  const { refetch: refetchMigrationAssesmentInfo } = useGetMigrationAssessmentInfoQuery(
    {
      uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchMigrationAssesmentSourceDB } = useGetAssessmentSourceDBInfoQuery(
    {
      uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchTargetRecommendation } = useGetAssessmentTargetRecommendationInfoQuery(
    {
      uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchMigrationSchemaTasks } = useGetVoyagerMigrateSchemaTasksQuery(
    {
      uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
    },
    { query: { enabled: false } }
  );

  const { refetch: refetchMigrationMetrics } = useGetVoyagerDataMigrationMetricsQuery(
    {
      uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
    },
    { query: { enabled: false } }
  );

  const refetch = React.useCallback(() => {
    refetchMigrationAssesmentDetails();
    refetchMigrationAssesmentInfo();
    refetchMigrationAssesmentSourceDB();
    refetchTargetRecommendation();
    refetchMigrationSchemaTasks();
    refetchMigrationMetrics();
  }, []);
  return (
    <Box>
      {stepComponents.map((StepComponent, index) => {
        if (index === step) {
          return (
            <StepComponent
              key={index}
              operatingSystem={index === 0 ? mAssessmentData?.operating_system : "git"}
              step={index}
              heading={steps[step]}
              migration={migration}
              onRefetch={refetch}
              isFetching={isFetching}
              isNewMigration={isNewMigration}
              voyagerVersion={mAssessmentData?.voyager_version ?? ""}
              notes={mAssessmentData?.notes ?? []}
            />
          );
        }
        return <React.Fragment key={index} />;
      })}
    </Box>
  );
};
