import React, { FC } from "react";
import { Box, LinearProgress, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../MigrationOverview";
import { STATUS_TYPES, YBAccordion, YBButton, YBStatus, YBTable } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";
import { MigrationStepNA } from "../MigrationStepNA";
import MigrationAccordionTitle from "../MigrationAccordionTitle";
import { ErrorRounded, InfoOutlined } from "@material-ui/icons";

const useStyles = makeStyles((theme) => ({
  heading: {
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
}));

const ComplexityComponent = (classes: ReturnType<typeof useStyles>) => (complexity: string) => {
  const className =
    complexity === "Hard"
      ? classes.hardComp
      : complexity === "Medium"
      ? classes.mediumComp
      : complexity === "Easy"
      ? classes.easyComp
      : undefined;

  return <Box className={className}>{complexity || "N/A"}</Box>;
};

interface MigrationPlanAssessProps {
  heading: string;
  migration: Migration;
  step: number;
  onRefetch: () => void;
}

export const MigrationPlanAssess: FC<MigrationPlanAssessProps> = ({ heading, onRefetch }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const isFetchingData = false;

  /* const assessmentAPI = {
    assesment_status: false,
    top_errors: [],
    top_suggestions: [],
    complexity_overview: [],
  }; */

  const assessmentAPI = {
    assesment_status: true,
    top_errors: ["error"],
    top_suggestions: ["suggestion"],
    complexity_overview: [
      {
        schema: "DMS",
        sql_objects_count: 6,
        table_count: 6,
        complexity: "Easy",
      },
    ],
  };

  const isComplete = assessmentAPI.assesment_status === true;

  const complexityColumns = [
    {
      name: "schema",
      label: t("clusterDetail.voyager.planAndAssess.schema"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "sql_objects_count",
      label: t("clusterDetail.voyager.planAndAssess.sqlObjectCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "table_count",
      label: t("clusterDetail.voyager.planAndAssess.tableCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "complexity",
      label: t("clusterDetail.voyager.planAndAssess.complexity"),
      options: {
        customBodyRender: ComplexityComponent(classes),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  return (
    <Box>
      <Box display="flex" justifyContent="space-between" alignItems="start">
        <Typography variant="h4" className={classes.heading}>
          {heading}
        </Typography>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefetch}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
      </Box>

      {isFetchingData && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!isFetchingData && (
        <>
          <Box display="flex" gridGap={4} alignItems="center">
            <Box px={!isComplete ? 1 : 0}>
              <YBStatus
                type={!isComplete ? STATUS_TYPES.PENDING : STATUS_TYPES.SUCCESS}
                size={!isComplete ? 16 : 42}
              />
            </Box>
            <Box display="flex" flexDirection="column">
              <Typography variant="h5">
                {!isComplete
                  ? t("clusterDetail.voyager.planAndAssess.assessmentPending")
                  : t("clusterDetail.voyager.planAndAssess.assessmentComplete")}
              </Typography>
              <Typography variant="body2">
                {!isComplete
                  ? t("clusterDetail.voyager.planAndAssess.assessmentPendingDesc")
                  : t("clusterDetail.voyager.planAndAssess.assessmentCompleteDesc")}
              </Typography>
            </Box>
          </Box>

          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)} mt={5}>
            <YBAccordion
              titleContent={
                <MigrationAccordionTitle
                  title={t("clusterDetail.voyager.planAndAssess.topSuggestions")}
                  count={assessmentAPI.top_suggestions.length}
                  color={theme.palette.warning[100]}
                />
              }
              defaultExpanded={isComplete}
            >
              {assessmentAPI.top_suggestions.length === 0 ? (
                <MigrationStepNA />
              ) : (
                <Box display="flex" gridGap={theme.spacing(1)} flexDirection="column" minWidth={0}>
                  {assessmentAPI.top_suggestions.map((suggestion) => (
                    <Box
                      key={suggestion}
                      display="flex"
                      alignItems="center"
                      gridGap={theme.spacing(1)}
                    >
                      <InfoOutlined color="primary" />
                      <Typography variant="body2">{suggestion}</Typography>
                    </Box>
                  ))}
                </Box>
              )}
            </YBAccordion>

            <YBAccordion
              titleContent={
                <MigrationAccordionTitle
                  title={t("clusterDetail.voyager.planAndAssess.topErrors")}
                  count={assessmentAPI.top_errors.length}
                  color={theme.palette.error[100]}
                />
              }
              defaultExpanded={isComplete}
            >
              {assessmentAPI.top_errors.length === 0 ? (
                <MigrationStepNA />
              ) : (
                <Box display="flex" gridGap={theme.spacing(1)} flexDirection="column" minWidth={0}>
                  {assessmentAPI.top_errors.map((error) => (
                    <Box key={error} display="flex" alignItems="center" gridGap={theme.spacing(1)}>
                      <ErrorRounded color="error" />
                      <Typography variant="body2">{error}</Typography>
                    </Box>
                  ))}
                </Box>
              )}
            </YBAccordion>

            <YBAccordion
              titleContent={t("clusterDetail.voyager.planAndAssess.complexityOverview")}
              defaultExpanded={isComplete}
            >
              {assessmentAPI.complexity_overview.length === 0 ? (
                <MigrationStepNA />
              ) : (
                <Box flex={1} px={2} minWidth={0}>
                  <YBTable
                    data={assessmentAPI.complexity_overview}
                    columns={complexityColumns}
                    options={{
                      pagination: true,
                    }}
                    withBorder={false}
                  />
                </Box>
              )}
            </YBAccordion>
          </Box>
        </>
      )}
    </Box>
  );
};
