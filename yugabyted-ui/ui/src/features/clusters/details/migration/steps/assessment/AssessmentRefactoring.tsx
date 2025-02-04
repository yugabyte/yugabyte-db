import React, { FC, useMemo } from "react";
import { Box, Divider, MenuItem, Link, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type {
  RefactoringCount,
  UnsupportedSqlInfo,
} from "@app/api/src";
import { RefactoringGraph } from "../schema/RefactoringGraph";
import { YBAccordion, YBSelect, YBTable } from "@app/components";
import { MigrationRefactoringIssueSidePanel } from "./AssessmentRefactoringIssueSidePanel";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.25),
    textTransform: "uppercase",
    textAlign: "left",
  },
  noGrow: {
    alignSelf: "flex-start",
  },
  divider: {
    marginTop: theme.spacing(2),
  },
  menuDivider: {
    margin: theme.spacing(1, 0, 1, 0),
  },
  tooltip: {
    backgroundColor: theme.palette.common.white,
    padding: theme.spacing(1),
    borderRadius: theme.shape.borderRadius,
    boxShadow: theme.shadows[2],
  },
  recommendationCard: {
    display: "flex",
    flexDirection: "column",
    justifyContent: "space-between",
    width: "100%",
  },
}));

interface MigrationAssessmentRefactoringProps {
  sqlObjects: RefactoringCount[] | undefined;
  unsupportedDataTypes: UnsupportedSqlInfo[] | undefined;
  unsupportedFeatures: UnsupportedSqlInfo[] | undefined;
  unsupportedFunctions: UnsupportedSqlInfo[] | undefined;
}

export const MigrationAssessmentRefactoring: FC<MigrationAssessmentRefactoringProps> = ({
  sqlObjects,
  unsupportedDataTypes,
  unsupportedFeatures,
  unsupportedFunctions,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const [typeFilter, setTypeFilter] = React.useState<string>("");
  const [showOccurrences, setShowOccurrences] = React.useState<boolean>(false);
  const [selectedIssue, setSelectedIssue] = React.useState<UnsupportedSqlInfo>();

  const hasGraphData = useMemo(
    () =>
      !!sqlObjects?.filter(({ automatic, manual }) => (automatic ?? 0) + (manual ?? 0) > 0)?.length,
    [sqlObjects]
  );

  if (
    !hasGraphData &&
    !unsupportedDataTypes?.length &&
    !unsupportedFeatures?.length &&
    !unsupportedFunctions?.length
  ) {
    return null;
  }
//   const getIssueType = (issueType: string) => {
//     const issueTypeMap: {[key: string]: string} = {
//       "unsupported_features":
//         t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.feature"),
//       "unsupported_datatypes":
//         t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.dataType"),
//     }
//     return issueTypeMap[issueType.toLowerCase()] ?? issueType;
//   };

//   const conversionIssuesData = useMemo(() => {
//     return conversionIssues?.map((issue) => {
//         issue.issueType = getIssueType(issue.issueType ?? "N/A");
//         return issue;
//     })?.filter((issue) => {
//         return typeFilter === "" || typeFilter === issue.issueType;
//     });
//   }, [conversionIssues, typeFilter]);

  const types = useMemo(() => {
    const typeSet = new Set<string>();
    if (unsupportedDataTypes?.length)
      typeSet.add(t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.dataType"));
    if (unsupportedFeatures?.length)
      typeSet.add(t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.feature"));
    if (unsupportedFunctions?.length)
      typeSet.add(t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.function"));
    return Array.from(typeSet);
  }, [unsupportedDataTypes, unsupportedFeatures, unsupportedFunctions]);

  const unsupportedObjectsData = useMemo(() => {
    const includeFeatures = typeFilter === "" ||
      typeFilter === t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.feature");
    const includeDataTypes = typeFilter === "" ||
      typeFilter === t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.dataType");
    const includeFunctions = typeFilter === "" ||
      typeFilter === t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.function");

    const features = includeFeatures
      ? unsupportedFeatures?.map((data) => ({
          ...data,
          issue_type:
            t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.feature"),
          sql_statement: data.objects?.length ? (data.objects[0].sql_statement || "N/A") : "N/A",
        })) ?? []
      : [];

    const dataTypes = includeDataTypes
      ? unsupportedDataTypes?.map((data) => ({
          ...data,
          issue_type:
            t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.dataType"),
          sql_statement: data.objects?.length ? (data.objects[0].sql_statement || "N/A") : "N/A",
        })) ?? []
      : [];

    const functions = includeFunctions
      ? unsupportedFunctions?.map((data) => ({
          ...data,
          issue_type:
            t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.function"),
          sql_statement: data.objects?.length ? (data.objects[0].sql_statement || "N/A") : "N/A",
        })) ?? []
      : [];

    return features.concat(dataTypes, functions);
  }, [unsupportedDataTypes, unsupportedFeatures, unsupportedFunctions, typeFilter]);

  const columns = [
    {
      name: "unsupported_type",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issue"),
      options: {
        setCellHeaderProps: () => ({ style: { width: "120px", padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "count",
      label:
        t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.occurrences"),
      options: {
        setCellHeaderProps: () => ({ style: { width: "130px", padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRenderLite: (dataIndex: number) => {
          if ((unsupportedObjectsData[dataIndex].count ?? 0) > 1) {
            return (
              <Link
                onClick={() => {
                  setSelectedIssue(unsupportedObjectsData[dataIndex]);
                  setShowOccurrences(true);
                }}
              >
                {unsupportedObjectsData[dataIndex].count}
              </Link>
            );
          }
          return unsupportedObjectsData[dataIndex].count;
        }
      },
    },
    {
      name: "issue_type",
      label:
        t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issueType"),
      options: {
        setCellHeaderProps: () => ({ style: { width: "120px", padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "sql_statement",
      label:
        t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.previewSqlStatement"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "docs_link",
      label:
        t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.workaround"),
      options: {
        setCellHeaderProps: () => ({ style: { width: "130px", padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (url: string) => (url
            ? <Link href={url} target="_blank">
                {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.linkToDocs")}
              </Link>
            : <>
                {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.notAvailable")}
              </>)
      },
    },
  ];


  return (<>
    <YBAccordion
      titleContent={
          t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.heading")}
      defaultExpanded
      contentSeparator
    >
      <Box className={classes.recommendationCard}>
        <RefactoringGraph sqlObjects={sqlObjects} />

        {unsupportedDataTypes?.length ||
        unsupportedFeatures?.length ||
        unsupportedFunctions?.length ? (
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(3)}>
            <Divider className={classes.divider}/>

            <YBSelect
              className={classes.noGrow}
              value={typeFilter}
              onChange={(e) => setTypeFilter(e.target.value)}
            >
              <MenuItem value="">
                {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
                    "allIssueTypes")}
              </MenuItem>
              <Divider className={classes.menuDivider}/>
              {types.map((type) => {
                return (
                  <MenuItem key={type} value={type}>
                    {type}
                  </MenuItem>
                );
              })}
            </YBSelect>

            <YBTable
              data={unsupportedObjectsData}
              columns={columns}
              withBorder={false}
            />
          </Box>
        ) : null}
      </Box>
    </YBAccordion>
    <MigrationRefactoringIssueSidePanel
      issue={selectedIssue}
      open={showOccurrences}
      onClose={() => setShowOccurrences(false)}
    />
  </>);
};
