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
}));

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
  {
    name: "tabComplexityOverview",
    testId: "ClusterTabList-ComplexityOverview",
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
      task: "Preparing",
      progress: "100%",
      status: "Complete",
    },
    {
      task: "Processing",
      progress: "45%",
      status: "In Progress",
    },
    {
      task: "Finalization",
      progress: "0%",
      status: "N/A",
    },
  ];

  const overviewColumns = [
    {
      name: "task",
      label: t("clusterDetail.voyager.planAndAssess.overview.task"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "progress",
      label: t("clusterDetail.voyager.planAndAssess.overview.progress"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "status",
      label: t("clusterDetail.voyager.planAndAssess.overview.status"),
      options: {
        customBodyRender: (status: string) =>
          status !== "N/A" ? (
            <YBBadge
              variant={status === "Complete" ? BadgeVariant.Success : BadgeVariant.InProgress}
              text={status}
            />
          ) : (
            status
          ),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  const nextStepsData = [
    {
      phase: "Resizing",
      status: "Complete",
    },
    {
      phase: "Processing",
      status: "In Progress",
    },
    {
      phase: "Finalization",
      status: "N/A",
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
          status !== "N/A" ? (
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
          <MigrationAccordionTitle title={t("clusterDetail.voyager.planAndAssess.overviewTab")} />
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
            {tab === "tabComplexityOverview" && (
              <MigrationComplexityOverview
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
                It is recommended to manually run validation queries on both the source and target
                database to ensure that the data is correctly migrated.
              </Typography>
            </li>
            <li>
              <Typography variant="body2">
                A sample query to validate the databases can include checking the row count of each
                table.
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
