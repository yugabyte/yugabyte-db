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

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  tabSectionContainer: {
    display: "flex",
    alignItems: "center",
    width: "100%",
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`,
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

  const data: MigrationAssesmentInfo = React.useMemo(
    () => ({
      assesment_status: true,
      complexity_overview: [
        {
          schema: "YUGABYTED",
          sql_objects_count: 10,
          table_count: 2,
          complexity: "Easy",
        },
      ],
      top_suggestions: ["Suggestion one", "Suggestion two"],
      top_errors: [],
    }),
    []
  );

  const isErrorMigrationAssessmentDetails = false;

  const assessmentAPI = React.useMemo(() => {
    const assessmentData = (data as MigrationAssesmentInfo) || {};
    assessmentData.top_suggestions = assessmentData.top_suggestions?.filter((s) => s.trim());
    assessmentData.top_errors = assessmentData.top_errors?.filter((s) => s.trim());
    return assessmentData;
  }, [data]);

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
        /*         customBodyRender: ComplexityComponent(classes), */
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  const [tab, setTab] = React.useState<string>(tabList[0].name);

  return (
    <Box mt={-2}>
      <Box display="flex" justifyContent="space-between" alignItems="start">
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
  );
};
