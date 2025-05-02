import React, { FC, useMemo } from "react";
import { Box, Divider, MenuItem, Link, makeStyles, useTheme, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { Trans } from "react-i18next";
import type {
  RefactoringCount,
  AssessmentCategoryInfo,
  UnsupportedSqlObjectData
} from "@app/api/src";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { RefactoringGraph } from "../schema/RefactoringGraph";
import { YBAccordion, YBSelect, YBTable, YBTooltip } from "@app/components";
import { MigrationRefactoringIssueSidePanel } from "./AssessmentRefactoringIssueSidePanel";
import { formatSnakeCase } from "@app/helpers";
import HelpIcon from "@app/assets/help.svg";
import { getComplexityString } from "../../ComplexityComponent";

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
  greyBox: {
    backgroundColor: '#f5f5f5',
    marginLeft: 20,
    display: 'flex',
    alignItems: 'center',
    borderRadius: 6,
    fontWeight: 500,
    border: '1px solid #eaeaea',
    borderColor: 'divider',
    padding: '8px 20px',
  },
  icon: {
    display: "flex",
    cursor: "pointer",
    color: theme.palette.grey[500],
    alignItems: "center",
  },
}));

interface MigrationAssessmentRefactoringProps {
  sqlObjects: RefactoringCount[] | undefined;
  assessmentCategoryInfo: AssessmentCategoryInfo[] | undefined;
  targetDBVersion?: string;
  migrationComplexityExplanation?: string | undefined;
}

export type UnsupportedObjectData = {
  issue_name?: string;
  issue_type?: string;
  count?: number;
  objects?: UnsupportedSqlObjectData[];
  docs_link?: string;
  category: string;
  sql_statement: string;
  impact: string;
  minimum_versions_fixed_in: string;
};



export const MigrationAssessmentRefactoring: FC<MigrationAssessmentRefactoringProps> = ({
  sqlObjects,
  assessmentCategoryInfo,
  targetDBVersion,
  migrationComplexityExplanation
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const translatedAllIssueTypes: string =
    t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.allIssueTypes");

  const ALL_OBJECTS: string = "All object types selected";
  const ALL_IMPACTS: string = t("clusterDetail.voyager.planAndAssess.impactLevels.allImpacts");
  const [typeFilter, setTypeFilter] = React.useState<string>(translatedAllIssueTypes);
  const [showOccurrences, setShowOccurrences] = React.useState<boolean>(false);
  const [selectedIssue, setSelectedIssue] = React.useState<UnsupportedObjectData>();
  const [selectedImpact, setSelectedImpact] = React.useState<string>(ALL_IMPACTS);


  const [selectedObjectType, setSelectedObjectType] = React.useState<string>(ALL_OBJECTS);

  const hasGraphData: boolean = useMemo(
    () => Array.isArray(sqlObjects) &&
      sqlObjects.some(({ automatic, manual }) => (automatic ?? 0) + (manual ?? 0) > 0),
    [sqlObjects]
  );

  const referenceToIssueTable: React.MutableRefObject<HTMLDivElement | null> =
    React.useRef<HTMLDivElement | null>(null);

  const scrollToIssueTable: () => void = () => {
    if (referenceToIssueTable.current) {
      referenceToIssueTable.current.scrollIntoView({ behavior: "smooth", block: "center" });
    }
  };

  const createCustomHeaderLabelRender = (
    labelKey: string,
    tooltipKey?: string,
    tooltipContent?: React.ReactNode
  ): () => React.ReactNode => {
    return () => (
      <Box display="inline-flex" alignItems="center">
        {t(labelKey)}
        {(tooltipContent || tooltipKey) && (
          <YBTooltip
            title={
              <Box>
                {tooltipContent || (tooltipKey ? t(tooltipKey) : null)}
              </Box>
            }
            placement="top"
            interactive
            classes={{
              tooltip: classes.tooltip
            }}
          />
        )}
      </Box>
    );
  };

  if (
    !hasGraphData &&
    !assessmentCategoryInfo?.length
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

  // Converting High Medium Low keywords to Hard Medium Easy
  const migCompexityExp: string | undefined = migrationComplexityExplanation
    ?.replaceAll(/\b(HIGH|MEDIUM|LOW)\b/gi, (match) =>
      getComplexityString(match.toLowerCase(), t)
    )
    .replace(/(\d+)\s+Level\s+\d+\s+issue\(s\)/g, (match, number) =>
      number === "1"
        ? match.replace(
          "issue(s)",
          t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issue")
        )
        : match.replace(
          "issue(s)",
          t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issues")
        )
    );

  // Making the Hard Medium Easy keywords bold
  const finalTranslationOutput =
    migCompexityExp?.split(/\b(Hard|Medium|Easy)\b/g).map((part, index) =>
      ["Hard", "Medium", "Easy"].includes(part) ? <strong key={index}>{part}</strong> : part
    );

  const types = useMemo(() => {
    const typeSet = new Set<string>();

    assessmentCategoryInfo?.forEach(({ category }) => {
      if (category) {
        typeSet.add(category);
      }
    });

    return Array.from(typeSet);
  }, [assessmentCategoryInfo]);

  // Although I could have inlined the description directly in each row,
  // the number of distinct categories is small, so a map provides a cleaner,
  // more efficient way to avoid redundant data duplication across rows.
  const categoryDescriptionMap = useMemo(() => {
    const map = new Map<string, string>();
    assessmentCategoryInfo?.forEach(({ category, category_description }) => {
      if (category) {
        map.set(formatSnakeCase(category), category_description ?? t('common.notAvailable'));
      }
    });
    return map;
  }, [assessmentCategoryInfo]);

  const unsupportedObjectsData = useMemo<UnsupportedObjectData[]>(() => {
    if (!assessmentCategoryInfo) return [];

    const showAllIssues: boolean = typeFilter === translatedAllIssueTypes;
    const showAllImpacts: boolean = selectedImpact === ALL_IMPACTS;

    return assessmentCategoryInfo
      .filter(({ category }) => showAllIssues || category === typeFilter)
      .reduce<UnsupportedObjectData[]>((unsupportedIssues, { category, issues = [] }) => {
        const issueData = issues.flatMap(({
          name, count, objects = [], docs_link, impact, type, minimum_versions_fixed_in }) => {

          const formattedImpact = formatSnakeCase(impact ?? "");
          if (!showAllImpacts && formattedImpact !== selectedImpact) {
            return [];
          }

          if (selectedObjectType === ALL_OBJECTS) {
            return [{
              issue_name: name,
              issue_type: type,
              count,
              objects,
              docs_link,
              category: formatSnakeCase(category ?? ""),
              sql_statement: objects.find(obj => obj?.sql_statement)?.sql_statement || "N/A",
              impact: formattedImpact,
              minimum_versions_fixed_in: minimum_versions_fixed_in?.join(", ") ?? "N/A"
            }];
          }

          const filteredObjects = objects.filter(obj =>
            obj.object_type?.toLowerCase() === selectedObjectType.toLowerCase()
          );

          if (filteredObjects.length === 0) {
            return [];
          }

          return [{
            issue_name: name,
            issue_type: type,
            count: filteredObjects.length,
            objects: filteredObjects,
            docs_link,
            category: formatSnakeCase(category ?? ""),
            sql_statement: filteredObjects.find(obj => obj?.sql_statement)?.sql_statement || "N/A",
            impact: formattedImpact,
            minimum_versions_fixed_in: minimum_versions_fixed_in?.join(", ") ?? "N/A"
          }];
        });
        return [...unsupportedIssues, ...issueData];
      }, []);
  }, [assessmentCategoryInfo, typeFilter, selectedObjectType, selectedImpact]);


  const columns = [
    {
      name: "issue_name",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issueName"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "2px 4px", maxWidth: "120px" } }),
        setCellProps: () => ({
          style: {
            padding: "2px 4px",
            maxWidth: "120px",
            wordBreak: "break-word",
            whiteSpace: "normal",
            hyphens: "auto"
          }
        }),
      },
    },
    {
      name: "count",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.occurrences"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "2px 4px", maxWidth: "60px" } }),
        setCellProps: () => ({
          style: {
            padding: "2px 4px",
            maxWidth: "60px", wordBreak: "break-word", whiteSpace: "normal", hyphens: "auto"
          }
        }),
        customBodyRenderLite: (dataIndex: number) => (
          <Link
            onClick={() => {
              setSelectedIssue(unsupportedObjectsData[dataIndex]);
              setShowOccurrences(true);
            }}
          >
            {unsupportedObjectsData[dataIndex].count}
          </Link>
        ),
      },
    },
    {
      name: "category",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.category"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "2px 4px", maxWidth: "100px" } }),
        setCellProps: () => ({
          style: {
            padding: "2px 4px",
            maxWidth: "100px",
            wordBreak: "break-word",
            whiteSpace: "normal",
            hyphens: "auto",
          },
        }),
        customBodyRenderLite: (dataIndex: number) => {
          const row = unsupportedObjectsData[dataIndex];
          const description = categoryDescriptionMap.get(row.category) || "No description";

          return (
            <YBTooltip
              title={description}
              placement="right"
              interactive
              classes={{
                tooltip: classes.tooltip
              }}
            >
              <Typography
                variant="body2"
                component="span"
                style={{
                  cursor: 'help',
                  borderBottom: `1px dotted ${theme.palette.grey[400]}`,
                  display: 'inline-flex',
                  alignItems: 'center'
                }}
              >
                {row.category}
              </Typography>
            </YBTooltip>
          );
        },
      },
    },
    {
      name: "impact",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.impact"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "2px 4px", maxWidth: "80px" } }),
        setCellProps: () => ({
          style: {
            padding: "2px 4px",
            maxWidth: "80px", wordBreak: "break-word", whiteSpace: "normal", hyphens: "auto"
          }
        }),
        customBodyRender: (value: string) => (
          <YBBadge text={formatSnakeCase(value ?? "N/A")} variant={BadgeVariant.Warning} />
        ),
        customHeadLabelRender: createCustomHeaderLabelRender(
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.impact",
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.impactTooltip",
          <Box>
            <Typography variant="body1">
              <Trans i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.title">
                <strong />
              </Trans>
            </Typography>
            <Typography variant="body2">
              <Trans i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level1">
                <strong />
              </Trans><br />
              <Trans i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level2">
                <strong />
              </Trans><br />
              <Trans i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level3">
                <strong />
              </Trans>
            </Typography>
          </Box>
        ),
      },
    },
    {
      name: "docs_link",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.workaround"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "2px 4px", maxWidth: "120px" } }),
        setCellProps: () => ({
          style: {
            padding: "2px 4px",
            maxWidth: "120px", wordBreak: "break-word", whiteSpace: "normal", hyphens: "auto"
          }
        }),
        customBodyRender: (url: string) =>
          url ? (
            <Link href={url} target="_blank">
              {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.linkToDocs")}
            </Link>
          ) : (
           <>{t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.notAvailable")}</>
          ),
      },
    },
    {
      name: "minimum_versions_fixed_in",
      label: t("clusterDetail.voyager.planAndAssess.recommendation." +
        "schemaChanges.minVersionsFixedIn"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "2px 4px", maxWidth: "90px" } }),
        setCellProps: () => ({
          style: {
            padding: "2px 4px",
            maxWidth: "90px",
            wordBreak: "break-word",
            whiteSpace: "normal",
            hyphens: "auto",
          },
        }),
        customHeadLabelRender: createCustomHeaderLabelRender(
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.minVersionsFixedIn",
        "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.minVersionsFixedInTooltip"
        ),
      },
    }

  ];



  return (
    <>
      <YBAccordion
        titleContent={
          <>
            {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.heading")}
            {targetDBVersion != null && (
              <Box className={classes.greyBox} display="flex" alignItems="center">
                {t("clusterDetail.voyager.planAndAssess.recommendation." +
                  "schemaChanges.targetDBVersion")}
                <Box marginLeft={1} fontWeight="bold">{targetDBVersion}</Box>

                <YBTooltip
                  title={
                    <Trans
                      i18nKey={"clusterDetail.voyager.planAndAssess.recommendation." +
                        "schemaChanges.targetDBVersionTooltip"}
                      components={{ bold: <Typography variant="body1" display="inline" /> }}
                    />
                  }
                >
                  <Box marginLeft={1} className={classes.icon}>
                    <HelpIcon />
                  </Box>
                </YBTooltip>
              </Box>
            )}
          </>
        }
        defaultExpanded
        contentSeparator
      >
        <Box className={classes.recommendationCard}>
          <RefactoringGraph
            sqlObjects={sqlObjects} isAssessmentPage={true}
            setSelectedObjectType={setSelectedObjectType}
            scrollToIssueTable={scrollToIssueTable}
          />
          {assessmentCategoryInfo?.length ? (
            <Box display="flex" flexDirection="column" gridGap={theme.spacing(3)}>
              <Divider className={classes.divider} />
              <Box>
                <Box display="flex" alignItems="center" gridGap={3} fontWeight={600}>
                  <Typography variant="h5">
                   {t("clusterDetail.voyager.planAndAssess.summary.migrationComplexityExplanation")}
                  </Typography>
                  <YBTooltip
                    title={
                      <Box>
                        <Typography variant="body1">
                          <Trans i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.title">
                            <strong />
                          </Trans>
                        </Typography>
                        <Typography variant="body2">
                          <Trans i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level1">
                            <strong />
                          </Trans><br />
                          <Trans i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level2">
                            <strong />
                          </Trans><br />
                          <Trans i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level3">
                            <strong />
                          </Trans>
                        </Typography>
                      </Box>
                    }
                  >
                    <Box display="flex" alignItems="center" ml={1}>
                      <HelpIcon />
                    </Box>
                  </YBTooltip>
                </Box>
                <Box marginTop={1.4}>
                  <Typography variant="body2">
                    {finalTranslationOutput}
                  </Typography>
                </Box>
              </Box>
              <Box>
                {/* div is needed here so as to use `ref` */}
                <div ref={referenceToIssueTable}>
                  <Box display="flex" alignItems="center" marginBottom="20px 0px">
                    <YBSelect
                      className={classes.noGrow}
                      style={{ marginRight: "16px" }}
                      value={typeFilter}
                      onChange={
                        (e: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>) =>
                          setTypeFilter(e.target.value)
                      }
                    >
                      <MenuItem value={translatedAllIssueTypes}>
                        {translatedAllIssueTypes}
                      </MenuItem>
                      <Divider className={classes.menuDivider} />
                      {types.map((type) => (
                        <MenuItem key={type} value={type}>
                          {formatSnakeCase(type)}
                        </MenuItem>
                      ))}
                    </YBSelect>
                    <YBSelect
                      className={classes.noGrow}
                      style={{ marginRight: "16px" }}
                      value={formatSnakeCase(selectedObjectType)}
                      onChange={(e) => setSelectedObjectType(e.target.value)}
                    >
                      <MenuItem value={ALL_OBJECTS}>{ALL_OBJECTS}</MenuItem>
                      <Divider className={classes.menuDivider} />
                      {sqlObjects?.map((sqlObject) => {
                        const formattedValue = formatSnakeCase(sqlObject?.sql_object_type ?? "");
                        return (
                          <MenuItem key={formattedValue} value={formattedValue}>
                            {formattedValue}
                          </MenuItem>
                        );
                      })}
                    </YBSelect>
                    <YBSelect
                      className={classes.noGrow}
                      value={selectedImpact}
                      onChange={(e) => setSelectedImpact(e.target.value)}
                    >
                      <MenuItem value={
                        t("clusterDetail.voyager.planAndAssess.impactLevels.allImpacts")}>{
                        t("clusterDetail.voyager.planAndAssess.impactLevels.allImpacts")
                      }
                      </MenuItem>
                      <MenuItem value={
                        t("clusterDetail.voyager.planAndAssess.impactLevels.Level1")
                      }>
                        {t("clusterDetail.voyager.planAndAssess.impactLevels.Level1")}
                      </MenuItem>
                      <MenuItem value={
                        t("clusterDetail.voyager.planAndAssess.impactLevels.Level2")
                      }>
                        {t("clusterDetail.voyager.planAndAssess.impactLevels.Level2")}
                      </MenuItem>
                      <MenuItem value={
                        t("clusterDetail.voyager.planAndAssess.impactLevels.Level3")
                      }>
                        {t("clusterDetail.voyager.planAndAssess.impactLevels.Level3")}
                      </MenuItem>
                    </YBSelect>
                  </Box>
                </div>
                <YBTable
                  data={unsupportedObjectsData}
                  columns={columns}
                  withBorder={false}
                />
              </Box>
            </Box>
          ) : null}
        </Box>
      </YBAccordion>

      <MigrationRefactoringIssueSidePanel
        issue={selectedIssue}
        description={assessmentCategoryInfo?.find(cat =>
            formatSnakeCase(cat.category || "") === selectedIssue?.category
          )?.issues?.find(issue =>
            issue.name === selectedIssue?.issue_name
          )?.description}
        open={showOccurrences}
        onClose={() => setShowOccurrences(false)}
      />

    </>

  );

};
