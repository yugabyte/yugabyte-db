import React, { FC } from "react";
import {
  Box,
  LinearProgress,
  makeStyles,
  Tab,
  Tabs,
  Typography,
  useTheme,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import { GenericFailure, STATUS_TYPES, YBAccordion, YBStatus, YBTable } from "@app/components";
import { MigrationStepNA } from "../../MigrationStepNA";
import MigrationAccordionTitle from "../../MigrationAccordionTitle";
import { ErrorRounded, InfoOutlined } from "@material-ui/icons";
import { MigrationAssesmentInfo, useGetVoyagerMigrationAssesmentDetailsQuery } from "@app/api/src";
import { MigrationAssessmentDetails } from "./AssessmentDetails";
import { MigrationAssessmentResults } from "./AssessmentResults";
import { MigrationComplexityOverview } from "./ComplexityOverview";
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
  isFetching?: boolean;
}

export const MigrationAssessment: FC<MigrationAssessmentProps> = ({
  heading,
  migration,
  onRefetch,
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
      result: "Result 1",
      complexity: "Easy",
      timeTaken: "1h 30m",
      tableSharding: "Colocated",
      targetClusterSizing: "No",
      vCpu: "4",
      ram: "16GB",
      disk: "100GB",
      nodeCount: "3",
    },
    {
      result: "Result 2",
      complexity: "Medium",
      timeTaken: "2h 45m",
      tableSharding: "Non-colocated",
      targetClusterSizing: "Yes",
      vCpu: "-",
      ram: "-",
      disk: "-",
      nodeCount: "-",
    },
    {
      result: "Result 3",
      complexity: "Hard",
      timeTaken: "5h 00m",
      tableSharding: "Colocated",
      targetClusterSizing: "Yes",
      vCpu: "-",
      ram: "-",
      disk: "-",
      nodeCount: "-",
    },
  ];

  const overviewColumns = [
    {
      name: "result",
      label: t("clusterDetail.voyager.planAndAssess.summary.result"),
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
      name: "tableSharding",
      label: t("clusterDetail.voyager.planAndAssess.summary.tableSharding"),
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
      name: "vCpu",
      label: t("clusterDetail.voyager.planAndAssess.summary.vCpu"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "ram",
      label: t("clusterDetail.voyager.planAndAssess.summary.ram"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "disk",
      label: t("clusterDetail.voyager.planAndAssess.summary.disk"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "nodeCount",
      label: t("clusterDetail.voyager.planAndAssess.summary.nodeCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
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
      },
    },
    {
      name: "status",
      label: t("clusterDetail.voyager.planAndAssess.nextSteps.status"),
      options: {
        customBodyRender: (status: string) =>
          status !== "-" ? (
            <YBBadge
              variant={status === "Complete" ? BadgeVariant.Success :  BadgeVariant.InProgress}
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
            }}
            withBorder={false}
          />
        </Box>
      </YBAccordion>
    </Box>
  );
};
