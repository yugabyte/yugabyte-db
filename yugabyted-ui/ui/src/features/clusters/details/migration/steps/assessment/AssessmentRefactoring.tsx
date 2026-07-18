import React, { FC, useMemo, useRef } from "react";
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
import ArrowRightIcon from "@app/assets/arrow-right.svg";
import ArrowRightIconBlue from "@app/assets/caretRightIconBlue.svg";
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
    maxWidth: 300,
    overflow: 'hidden',
    wordWrap: 'break-word',
    zIndex: 1500
  },
  recommendationCard: {
    display: "flex",
    flexDirection: "column",
    justifyContent: "space-between",
    width: "100%",
    position: "relative",
  },
  greyBox: {
    backgroundColor: '#f5f5f5',
    marginLeft: theme.spacing(2.5),
    display: 'flex',
    alignItems: 'center',
    borderRadius: theme.spacing(0.75),
    fontWeight: theme.typography.button.fontWeight,
    border: '1px solid #eaeaea',
    borderColor: 'divider',
    padding: theme.spacing(1, 2.5),
  },
  titleContainer: {
    display: 'flex',
    alignItems: 'center'
  },
  separator: {
    margin: theme.spacing(0, 1)
  },
  versionText: {
    color: theme.palette.text.primary,
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.subtitle1.fontSize,
    fontStyle: "normal",
    fontWeight: theme.typography.body2.fontWeight,
    lineHeight: "16px",
    display: "flex",
    alignItems: "center"
  },
  versionValue: {
    marginLeft: theme.spacing(1)
  },
  issueBox: {
    display: 'flex',
    padding: theme.spacing(2),
    marginBottom: theme.spacing(4),
    flexDirection: 'column',
    alignItems: 'flex-start',
    alignSelf: 'stretch',
    backgroundColor: theme.palette.background.default,
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.divider}`,
    '& > *:not(:last-child)': {
      marginBottom: theme.spacing(1)
    }
  },
  filterContainer: {
    display: 'flex',
    alignItems: 'center',
    '& > *:not(:last-child)': {
      marginRight: theme.spacing(1)
    }
  },
  translationContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  }
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

  // Ensure smooth scrolling is enabled globally
  React.useEffect(() => {
    const originalScrollBehavior = document.documentElement.style.scrollBehavior;
    document.documentElement.style.scrollBehavior = 'smooth';

    return () => {
      document.documentElement.style.scrollBehavior = originalScrollBehavior;
    };
  }, []);

  const translatedAllIssueTypes: string =
    t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.allIssueTypes");

  const ALL_OBJECTS: string = "All object types";
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

  const objectTypeSelectRef = useRef<HTMLInputElement>(null);
  const filterSectionRef = useRef<HTMLDivElement>(null);

  const scrollToIssueTable: () => void = () => {
    if (filterSectionRef.current) {
      filterSectionRef.current.scrollIntoView({
        behavior: "smooth",
        block: "start",
      });
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

  // Making the Hard/Medium/Easy migration phrases bold
  const finalTranslationOutput = migCompexityExp?.replace(
    /(Hard|Medium|Easy)\s+migration/g,
    (match) => `<strong>${match}</strong>`
  );

  const renderTranslationOutput = (
    <Typography variant="body2" className={classes.translationContainer}>
      <ArrowRightIcon />
      <span dangerouslySetInnerHTML={{ __html: finalTranslationOutput || '' }} />
    </Typography>
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

  // Although we could have inlined the description directly in each row,
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
        setCellHeaderProps: () => ({
          style: { padding: theme.spacing(0.25, 0.5), maxWidth: theme.spacing(15) },
        }),
        setCellProps: () => ({
          style: {
            padding: theme.spacing(0.25, 0.5),
            maxWidth: theme.spacing(15),
            wordBreak: "break-word",
            whiteSpace: "normal",
            hyphens: "auto"
          }
        }),
      },
    },
    {
      name: "category",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.category"),
      options: {
        setCellHeaderProps: () => ({
          style: { padding: theme.spacing(0.25, 0.5), maxWidth: theme.spacing(12.5) },
        }),
        setCellProps: () => ({
          style: {
            padding: theme.spacing(0.25, 0.5),
            maxWidth: theme.spacing(12.5),
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
        setCellHeaderProps: () => ({
          style: { padding: theme.spacing(0.25, 0.5), maxWidth: theme.spacing(10) },
        }),
        setCellProps: () => ({
          style: {
            padding: theme.spacing(0.25, 0.5),
            maxWidth: theme.spacing(10),
            wordBreak: "break-word",
            whiteSpace: "normal",
            hyphens: "auto",
          },
        }),
        customBodyRender: (value: string) => (
          <YBBadge
            text={
                  <Typography
                    style={{ minWidth: '45px', display: 'inline-block', textAlign: 'left' }}
                    variant="body2">{formatSnakeCase(value ?? "N/A")}
                  </Typography>
                }
            variant={BadgeVariant.Warning}
          />
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
              <Trans
                i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level1">
                <strong />
              </Trans><br />
              <Trans
                i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level2">
                <strong />
              </Trans><br />
              <Trans
                i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level3"
              >
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
        setCellHeaderProps: () => ({
          style: { padding: theme.spacing(0.25, 0.5), maxWidth: theme.spacing(15) },
        }),
        setCellProps: () => ({
          style: {
            padding: theme.spacing(0.25, 0.5),
            maxWidth: theme.spacing(15),
            wordBreak: "break-word",
            whiteSpace: "normal",
            hyphens: "auto",
          },
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
      name: "count",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.occurrences"),
      options: {
        setCellHeaderProps: () => ({
          style: { padding: theme.spacing(0.25, 0.5), maxWidth: theme.spacing(7.5) },
        }),
        setCellProps: () => ({
          style: {
            padding: theme.spacing(0.25, 0.5),
            maxWidth: theme.spacing(7.5),
            wordBreak: "break-word",
            whiteSpace: "normal",
            hyphens: "auto",
          },
        }),
        customBodyRenderLite: (dataIndex: number) => (
          <Box display="flex" alignItems="center">
            <Box width="24px" textAlign="left">
              {unsupportedObjectsData[dataIndex].count}
            </Box>
            <Link
              onClick={() => {
                setSelectedIssue(unsupportedObjectsData[dataIndex]);
                setShowOccurrences(true);
              }}
              style={{
                fontSize: theme.typography.subtitle1.fontSize
              }}
            >
              {t("clusterDetail.voyager.planAndAssess.refactoring.seeDetails")}
            </Link>
          </Box>
        ),
      },
    },
    {
      name: "minimum_versions_fixed_in",
      label:
        t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.minVersionsFixedIn"),
      options: {
        setCellHeaderProps: () => ({
          style: { padding: theme.spacing(0.25, 0.5), maxWidth: theme.spacing(11.25) },
        }),
        setCellProps: () => ({
          style: {
            padding: theme.spacing(0.25, 0.5),
            maxWidth: theme.spacing(11.25),
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
    },
    {
      name: "rightArrowSidePanel",
      options: {
        sort: false,
        setCellHeaderProps: () => ({
          style: { width: theme.spacing(6.25), padding: theme.spacing(1) },
        }),
        setCellProps: () => ({ style: { width: theme.spacing(6.25), padding: theme.spacing(1) } }),
        customBodyRenderLite: (dataIndex: number) => (
          <Box display="flex" alignItems="center">
            <Link
              onClick={() => {
                setSelectedIssue(unsupportedObjectsData[dataIndex]);
                setShowOccurrences(true);
              }}
            >
              <ArrowRightIconBlue />
            </Link>
          </Box>
        ),
        customHeadLabelRender: () => null,
      },
    },

  ];



  return (
    <>
      <YBAccordion
        titleContent={
          <Box className={classes.titleContainer}>
            {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.heading")}
            {targetDBVersion != null && (
              <>
                <Typography className={classes.separator} component="span">-</Typography>
                <Typography
                  className={classes.versionText}
                  component="span"
                >
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges."
                    + "targetDBVersion")}   {targetDBVersion}
                  <YBTooltip
                    title={
                      <Trans
                        i18nKey={"clusterDetail.voyager.planAndAssess.recommendation.schemaChanges."
                          + "targetDBVersionTooltip"}
                      />
                    }
                  />
                </Typography>
              </>
            )}
          </Box>
        }
        defaultExpanded
        contentSeparator
      >
        <Box className={classes.recommendationCard}>
          <RefactoringGraph
            sqlObjects={sqlObjects} isAssessmentPage={true}
            setSelectedObjectType={setSelectedObjectType}
            scrollToIssueTable={scrollToIssueTable}
            objectTypeSelectRef={objectTypeSelectRef}
          />
          {assessmentCategoryInfo?.length ? (
            <Box display="flex" flexDirection="column" gridGap={theme.spacing(3)}>
              <Box
                sx={{
                  display: 'flex',
                  padding: '24px',
                  flexDirection: 'column',
                  alignItems: 'flex-start',
                  alignSelf: 'stretch',
                  borderRadius: '8px',
                  border: '1px solid var(--Greyscale-Greyscale-200, #E9EEF2)',
                  marginBottom: '56px',
                  width: '100%'
                }}
              >
                <Box width="100%" mb="24px">
                  <Box
                    display="flex" alignItems="center" gridGap={3}
                    fontWeight={600}
                    style={{ fontSize: '13px' }}
                  >
                    <Typography variant="h5">
                     {t("clusterDetail.voyager.planAndAssess.summary."
                      + "migrationComplexityExplanation")}
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
                            <Trans
                              i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level1">
                              <strong />
                            </Trans><br />
                            <Trans
                              i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level2">
                              <strong />
                            </Trans><br />
                            <Trans
                              i18nKey="clusterDetail.voyager.planAndAssess.impactLevels.level3"
                            >
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
                </Box>
                <Box width="100%" marginTop={1.4}>
                  <div ref={filterSectionRef}>
                    <Box
                      className={classes.issueBox}
                    >
                      <Box className={classes.filterContainer}>
                        <YBSelect
                          className={classes.noGrow}
                          value={typeFilter}
                          onChange={
                            (e: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>) =>
                              setTypeFilter(e.target.value)
                          }
                          style={{ minWidth: '160px' }}
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
                          ref={objectTypeSelectRef}
                          className={classes.noGrow}
                          value={formatSnakeCase(selectedObjectType)}
                          onChange={(e) => setSelectedObjectType(e.target.value)}
                          style={{ minWidth: '160px' }}
                        >
                          <MenuItem value={ALL_OBJECTS} selected={
                            formatSnakeCase(selectedObjectType) === ALL_OBJECTS
                           }>
                            {ALL_OBJECTS}
                          </MenuItem>
                          <Divider className={classes.menuDivider} />
                          {sqlObjects?.map((sqlObject) => {
                            const formattedValue =
                              formatSnakeCase(sqlObject?.sql_object_type ?? "");
                            return (
                              <MenuItem
                                key={formattedValue}
                                value={formattedValue}
                                selected={formatSnakeCase(selectedObjectType) === formattedValue}
                              >
                                {formattedValue}
                              </MenuItem>
                            );
                          })}
                        </YBSelect>
                        <YBSelect
                          className={classes.noGrow}
                          value={selectedImpact}
                          onChange={(e) => setSelectedImpact(e.target.value)}
                          style={{ minWidth: '160px' }}
                        >
                          <MenuItem value={
                            String(t("clusterDetail.voyager.planAndAssess.impactLevels.allImpacts"))
                          }>
                            {t("clusterDetail.voyager.planAndAssess.impactLevels.allImpacts")}
                          </MenuItem>
                          <MenuItem value={
                            String(t("clusterDetail.voyager.planAndAssess.impactLevels.Level1"))
                          }>
                            {t("clusterDetail.voyager.planAndAssess.impactLevels.Level1")}
                          </MenuItem>
                          <MenuItem value={
                            String(t("clusterDetail.voyager.planAndAssess.impactLevels.Level2"))
                          }>
                            {t("clusterDetail.voyager.planAndAssess.impactLevels.Level2")}
                          </MenuItem>
                          <MenuItem value={
                            String(t("clusterDetail.voyager.planAndAssess.impactLevels.Level3"))
                          }>
                            {t("clusterDetail.voyager.planAndAssess.impactLevels.Level3")}
                          </MenuItem>
                        </YBSelect>
                      </Box>
                      {renderTranslationOutput}
                    </Box>
                  </div>
                  <Box sx={{ marginTop: '24px' }}>
                    <YBTable
                      data={unsupportedObjectsData}
                      columns={columns}
                      withBorder={false}
                    />
                  </Box>
                </Box>
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
