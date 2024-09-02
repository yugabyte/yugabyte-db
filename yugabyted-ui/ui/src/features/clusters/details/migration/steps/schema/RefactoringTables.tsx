import React, { FC } from "react";
import { Box } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { UnsupportedSqlInfo } from "@app/api/src";
import { MigrationAssessmentRefactoringTable } from "../assessment/AssessmentRefactoringTable";

interface RefactoringTablesProps {
  unsupportedDataTypes?: UnsupportedSqlInfo[] | undefined;
  unsupportedFeatures?: UnsupportedSqlInfo[] | undefined;
  enableMoreFeatureDetails?: boolean
  unsupportedFunctions?: UnsupportedSqlInfo[] | undefined;
}

export const RefactoringTables: FC<RefactoringTablesProps> = ({
  unsupportedDataTypes,
  unsupportedFeatures,
  unsupportedFunctions,
  enableMoreFeatureDetails,
}) => {
  const { t } = useTranslation();

  return (
    <Box display="flex" flexDirection="column" gridGap={20}>
      {unsupportedDataTypes?.length ? (
        <MigrationAssessmentRefactoringTable
          data={unsupportedDataTypes}
          tableHeader={t("clusterDetail.voyager.planAndAssess.refactoring.unsupportedDataType")}
          title={t("clusterDetail.voyager.planAndAssess.refactoring.datatype")}
        />
      ) : null}
      {unsupportedFeatures?.length ? (
        <MigrationAssessmentRefactoringTable
          data={unsupportedFeatures}
          tableHeader={t("clusterDetail.voyager.planAndAssess.refactoring.unsupportedFeature")}
          title={t("clusterDetail.voyager.planAndAssess.refactoring.feature")}
          enableMoreDetails={enableMoreFeatureDetails}
        />
      ) : null}
      {unsupportedFunctions?.length ? (
        <MigrationAssessmentRefactoringTable
          data={unsupportedFunctions}
          tableHeader={t("clusterDetail.voyager.planAndAssess.refactoring.unsupportedFunction")}
          title={t("clusterDetail.voyager.planAndAssess.refactoring.function")}
        />
      ) : null}
    </Box>
  );
};
