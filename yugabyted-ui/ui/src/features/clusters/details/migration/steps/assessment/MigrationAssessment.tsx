import React, { FC } from "react";
import { Box, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import { useGetVoyagerMigrationAssesmentDetailsQuery } from "@app/api/src";
import { MigrationAssessmentSummary } from "./AssessmentSummary";
import { MigrationSourceEnv } from "./AssessmentSourceEnv";
import { MigrationAssessmentRecommendation } from "./AssessmentRecommendation";
import { MigrationAssessmentRefactoring } from "./AssessmentRefactoring";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(2),
  },
  tabSectionContainer: {
    display: "flex",
    alignItems: "center",
    width: "100%",
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`,
  },
  nextSteps: {
    paddingLeft: theme.spacing(4),
    marginBottom: theme.spacing(4),
  },
  hardComp: {
    color: theme.palette.error.main,
  },
  mediumComp: {
    color: theme.palette.warning[700],
  },
  easyComp: {
    color: theme.palette.success.main,
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textAlign: "left",
  },
}));

interface MigrationAssessmentProps {
  heading: string;
  migration: Migration;
  step: number;
  onRefetch: () => void;
  onStepChange?: (step: number) => void;
  isFetching?: boolean;
}

export const MigrationAssessment: FC<MigrationAssessmentProps> = ({
  heading,
  migration,
  onRefetch,
  onStepChange,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  // DATO
  const {
    data: dato,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationAssessmentDetailso,
  } = useGetVoyagerMigrationAssesmentDetailsQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });

  const newMigration = {
    completedTime: "Completed 04/02/2024, 08:26 PDT",
    summary: {
      complexity: "Easy",
      estimatedMigrationTime: "2 hr 39 mins",
    },
    sourceEnv: {
      vcpu: "240",
      memory: "480 GB",
      disk: "960 GB",
      connectionCount: "16",
      tableSize: "240 GB",
      indexSize: "30 GB",
      totalSize: "270 GB",
      rowCount: "120,392,668",
    },
    recommendation: {
      description:
        "Recommended instance with 16 vCPU and 64 GB memory could fit 7 objects with size 73 GB as colocated. Rest 3 objects of size 281 GB can be imported as sharded tables.",
      clusterSize: {
        nodeCount: "15",
        vcpuPerNode: "16",
        memoryPerNode: "64 GB",
        optSelectConnPerNode: "40",
        optInsertConnPerNode: "48",
      },
      schemaRecommendation: {
        colocatedTables: "7",
        colocatedSize: "73 GB",
        shardedTables: "3",
        shardedSize: "281 GB",
      },
    },
    refactoring: {
      schemaList: ["public", "schema-01", "schema-02"],
      sqlObjects: [
        {
          objectType: "Type",
          automaticDDLImport: 25,
          manualRefactoring: 3,
        },
        {
          objectType: "Table",
          automaticDDLImport: 72,
          manualRefactoring: 5,
        },
        {
          objectType: "View",
          automaticDDLImport: 27,
          manualRefactoring: 3,
        },
        {
          objectType: "Function",
          automaticDDLImport: 22,
          manualRefactoring: 9,
        },
        {
          objectType: "Triggers",
          automaticDDLImport: 23,
          manualRefactoring: 5,
        },
      ],
      suggestionsErrors: [
        {
          datatype: "JSONB",
          objects: [
            {
              type: "View",
              filePath: "/home/nikhil/tradex/schema/views/view.sql",
              sql: `CREATE or REPLACE VIEW stock_trend (symbol_id, trend) AS (select trade_symbol as symbol_id, JSON_ARRAYAGG(trunc(high_price, 2) order by price_time DESC) as trend FROM trade_symbol_price_history tsph where interval_period = ‘1DAY’ group by trade_symbol_id);`,
            },
            {
              type: "Table",
              filePath: "/home/nikhil/tradex/schema/tables/table.sql",
              sql: `CREATE TABLE stock_trend (symbol_id INT, trend JSONB);`,
            },
            {
              type: "View",
              filePath: "/home/nikhil/tradex/schema/views/view.sql",
              sql: `CREATE or REPLACE VIEW stock_trend (symbol_id, trend) AS (select trade_symbol as symbol_id, JSON_ARRAYAGG(trunc(high_price, 2) order by price_time DESC) as trend FROM trade_symbol_price_history tsph where interval_period = ‘1DAY’ group by trade_symbol_id);`,
            },
            {
              type: "View",
              filePath: "/home/nikhil/tradex/schema/views/view2.sql",
              sql: `CREATE or REPLACE VIEW stock_trend (symbol_id, trend) AS (select trade_symbol as symbol_id, JSON_ARRAYAGG(trunc(high_price, 2) order by price_time DESC) as trend FROM trade_symbol_price_history tsph where interval_period = ‘1DAY’ group by trade_symbol_id);`,
            },
          ],
        },
        {
          datatype: "Function",
          objects: [],
        },
      ],
    },
  } as const;

  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
      <Box display="flex" justifyContent="space-between" alignItems="center">
        <Typography variant="h4" className={classes.heading}>
          {t("clusterDetail.voyager.planAndAssess.heading")}
        </Typography>
        {newMigration.completedTime && (
          <Typography variant="body1" className={classes.label}>
            {newMigration.completedTime}
          </Typography>
        )}
      </Box>

      <MigrationAssessmentSummary {...newMigration.summary} />

      <MigrationSourceEnv {...newMigration.sourceEnv} />

      <MigrationAssessmentRecommendation {...newMigration.recommendation} />

      <MigrationAssessmentRefactoring {...newMigration.refactoring} />
    </Box>
  );
};
