import React, { FC } from "react";
import { Box, makeStyles, Tab, Tabs, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import { YBAccordion, YBTable } from "@app/components";
import MigrationAccordionTitle from "../../MigrationAccordionTitle";
import { useGetVoyagerMigrationAssesmentDetailsQuery } from "@app/api/src";
import { MigrationAssessmentDetails } from "./AssessmentDetails";
import { MigrationAssessmentResults } from "./AssessmentResults";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";

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
}));

const ComplexityComponent = (classes: ReturnType<typeof useStyles>) => (complexity: string) => {
  const complexityL = complexity.toLowerCase();

  const className =
    complexityL === "hard"
      ? classes.hardComp
      : complexityL === "medium"
      ? classes.mediumComp
      : complexityL === "easy"
      ? classes.easyComp
      : undefined;

  return <Box className={className}>{complexity || "N/A"}</Box>;
};

export interface ITabListItem {
  name: string;
  testId: string;
}

const tabList: ITabListItem[] = [
  {
    name: "tabAssessmentDetails",
    testId: "ClusterTabList-AssessmentDetails",
  },
  {
    name: "tabAssessmentResults",
    testId: "ClusterTabList-AssessmentResults",
  },
];

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

  const [tab, setTab] = React.useState<string>(tabList[0].name);

  const overviewData = [
    {
      iteration: "1",
      complexity: "Easy",
      timeTaken: "1h 30m",
      colocatedTables: 10,
      nonColocatedTables: 5,
      targetClusterSizing: "No",
      recommendedClusterSizing: "4 vCPU, 16GB RAM, 100GB Disk, 3 Nodes",
    },
    {
      iteration: "2",
      complexity: "Medium",
      timeTaken: "2h 15m",
      colocatedTables: 8,
      nonColocatedTables: 7,
      targetClusterSizing: "Yes",
      recommendedClusterSizing: "-",
    },
    {
      iteration: "3",
      complexity: "Hard",
      timeTaken: "3h 45m",
      colocatedTables: 5,
      nonColocatedTables: 10,
      targetClusterSizing: "Yes",
      recommendedClusterSizing: "-",
    },
  ];

  const overviewColumns = [
    {
      name: "iteration",
      label: t("clusterDetail.voyager.planAndAssess.summary.iteration"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "complexity",
      label: t("clusterDetail.voyager.planAndAssess.summary.complexity"),
      options: {
        customBodyRender: ComplexityComponent(classes),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "timeTaken",
      label: t("clusterDetail.voyager.planAndAssess.summary.timeTaken"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "colocatedTables",
      label: t("clusterDetail.voyager.planAndAssess.summary.colocatedTables"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "nonColocatedTables",
      label: t("clusterDetail.voyager.planAndAssess.summary.nonColocatedTables"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "targetClusterSizing",
      label: t("clusterDetail.voyager.planAndAssess.summary.targetClusterSizing"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "recommendedClusterSizing",
      label: t("clusterDetail.voyager.planAndAssess.summary.recommendedClusterSizing"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px", maxWidth: "40px" } }),
      },
    },
  ];

  const nextStepsData = [
    {
      phase: "Resize",
      status: "Complete",
    },
    {
      phase: "Migrate Schema",
      status: "In Progress",
    },
    {
      phase: "Migrate Data",
      status: "-",
    },
    {
      phase: "Verify Performance",
      status: "-",
    },
  ];

  const nextStepsColumns = [
    {
      name: "phase",
      label: t("clusterDetail.voyager.planAndAssess.nextSteps.phase"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        sort: false,
      },
    },
    {
      name: "status",
      label: t("clusterDetail.voyager.planAndAssess.nextSteps.status"),
      options: {
        sort: false,
        customBodyRender: (status: string) =>
          status !== "-" ? (
            <YBBadge
              variant={status === "Complete" ? BadgeVariant.Success : BadgeVariant.InProgress}
              text={status}
            />
          ) : (
            status
          ),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px", height: "44px" } }),
      },
    },
  ];

  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
      <Typography variant="h4" className={classes.heading}>
        {heading}
      </Typography>

      <YBAccordion
        titleContent={
          <MigrationAccordionTitle title={t("clusterDetail.voyager.planAndAssess.summaryTab")} />
        }
        defaultExpanded
      >
        <Box flex={1} px={2} minWidth={0}>
          <YBTable
            data={overviewData}
            columns={overviewColumns}
            options={{
              pagination: true,
            }}
            withBorder={false}
          />
        </Box>
      </YBAccordion>

      <YBAccordion
        titleContent={
          <MigrationAccordionTitle title={t("clusterDetail.voyager.planAndAssess.assessmentTab")} />
        }
      >
        <Box flex={1} px={2} minWidth={0}>
          <Box className={classes.tabSectionContainer}>
            <Tabs
              value={tab}
              indicatorColor="primary"
              textColor="primary"
              data-testid="ClusterTabList"
            >
              {tabList.map((tab) => (
                <Tab
                  key={tab.name}
                  value={tab.name}
                  label={t(`clusterDetail.voyager.planAndAssess.${tab.name}`)}
                  onClick={() => setTab(tab.name)}
                  data-testid={tab.testId}
                />
              ))}
            </Tabs>
          </Box>

          <Box mt={2}>
            {tab === "tabAssessmentDetails" && (
              <MigrationAssessmentDetails
                heading={heading}
                migration={migration}
                onRefetch={onRefetch}
                isFetching={isFetching}
              />
            )}
            {tab === "tabAssessmentResults" && (
              <MigrationAssessmentResults
                heading={heading}
                migration={migration}
                onRefetch={onRefetch}
                isFetching={isFetching}
              />
            )}
          </Box>
        </Box>
      </YBAccordion>

      <YBAccordion
        titleContent={
          <MigrationAccordionTitle title={t("clusterDetail.voyager.planAndAssess.nextStepsTab")} />
        }
      >
        <Box flex={1} px={2} minWidth={0}>
          <ul className={classes.nextSteps}>
            <li>
              <Typography variant="body2">
                Ensure that the assessment results are reviewed and validated before proceeding with
                the migration.
              </Typography>
            </li>
            <li>
              <Typography variant="body2">
                The next steps will guide you through the migration process.
              </Typography>
            </li>
          </ul>

          <YBTable
            data={nextStepsData}
            columns={nextStepsColumns}
            options={{
              pagination: true,
              onRowClick: (_, { dataIndex }) => {
                console.log(migration.migration_phase)
                if (dataIndex === 1 || dataIndex === 2) {
                  onStepChange?.(dataIndex);
                }
              },
            }}
            withBorder={false}
          />
        </Box>
      </YBAccordion>
    </Box>
  );
};
