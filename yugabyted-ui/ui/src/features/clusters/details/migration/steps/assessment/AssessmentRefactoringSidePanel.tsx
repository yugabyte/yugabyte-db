import React, { FC, useMemo } from "react";
import { Box, Divider, MenuItem, TablePagination, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBAccordion, YBInput, YBModal, YBSelect, YBToggle, YBCodeBlock } from "@app/components";
import SearchIcon from "@app/assets/search.svg";
import ExternalLink from "@app/assets/external-link.svg"
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import type { SqlObjectsDetails } from "@app/api/src";
import { getMappedData } from "./refactoringUtils";
import { formatSnakeCase, useQueryParams } from "@app/helpers";
import { Link } from "@material-ui/core";
import GithubIcon from "@app/assets/github.svg";
import WarningIcon from "@app/assets/alert-solid.svg";
import YBLogo from "@app/assets/yb-logo.svg";
import { YBTable } from "@app/components";

const useStyles = makeStyles((theme) => ({
  assessmentBadge: {
    height: theme.spacing(3),
    padding: theme.spacing(0.5, 0.75),
    gap: theme.spacing(0.5),
    borderRadius: theme.spacing(0.75),
    background: theme.palette.primary[100],
    color: theme.palette.primary[600],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.body2.fontWeight,
    lineHeight: '16px',
  },
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    textTransform: "uppercase",
    textAlign: "left",
    fontSize: '11.5px',
    fontWeight: 500,
    color: '#6D7C88',
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  pointer: {
    cursor: "pointer",
  },
  grayBg: {
    backgroundColor: theme.palette.background.default,
    borderRadius: theme.shape.borderRadius,
  },
  fullWidth: {
    width: "100%",
  },
  divider: {
    margin: theme.spacing(1, 0, 1, 0),
  },
  dividerNew: {
    margin: theme.spacing(1, 0, 1, 0),
    backgroundColor: "#181818",
  },
  queryCodeBlock: {
    lineHeight: 1.5,
    padding: theme.spacing(1),
    fontSize: '12px',
  },
  toggleSwitch: {
    flexShrink: 0,
  },
  icon: {
    height: "14px",
    width: "14px",
    marginBottom: "2px",
    color: theme.palette.warning[500],
  },
  warningBox: {
    background: theme.palette.warning[100],
    color: theme.palette.warning[900],
    padding: `${theme.spacing(0.6)}px ${theme.spacing(1)}px`,
    borderRadius: theme.shape.borderRadius,
  },
  githubIcon: {
    height: "40px",
    width: "40px",
    marginBottom: "2px",
  },
  sqlSection: {
    marginBottom: theme.spacing(2),
    marginTop: theme.spacing(3),
  },
  linkContainer: {
    display: "flex",
    alignItems: "center",
    gap: theme.spacing(2),
    marginTop: theme.spacing(2),
  },
  linkItem: {
    display: "flex",
    alignItems: "center",
    gap: theme.spacing(1),
  },
}));

interface MigrationRefactoringSidePanelProps {
  data: SqlObjectsDetails | undefined;
  onClose: () => void;
  header: string;
  title: string;
  acknowledgedObjects?: {
    [key: string]: {
      [key: string]: {
        [key: string]: {
          [key: string]: boolean;
        };
      };
    };
  };
  toggleAcknowledgment?: (filePath: string, sqlStatement: string, reason: string) => void;
}

export const MigrationRefactoringSidePanel: FC<MigrationRefactoringSidePanelProps> = ({
  data,
  onClose,
  header,
  title,
  acknowledgedObjects,
  toggleAcknowledgment,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [page, setPage] = React.useState<number>(0);
  const [perPage, setPerPage] = React.useState<number>(5);
  const [totalItemCount, setTotalItemCount] = React.useState<number>(0);
  const [search, setSearch] = React.useState<string>("");
  const [selectedAck, setSelectedAck] = React.useState<string>("All");
  const [currValue, setCurrValue] = React.useState<number>(0);
  const [selectedIssueTypeFilter, setSelectedIssueTypeFilter] = React.useState<string>("Any");
  const [localAcknowledgedObjects, setLocalAcknowledgedObjects] = React.useState<{
    [key: string]: {
      [key: string]: {
        [key: string]: {
          [key: string]: boolean;
        };
      };
    };
  }>({});

  const queryParams = useQueryParams();
  const migrationUUID: string = queryParams.get("migration_uuid") ?? "";

  React.useEffect(() => {
    setLocalAcknowledgedObjects(acknowledgedObjects || {});
  }, [acknowledgedObjects]);

  const handleLocalToggle = (filePath: string, sqlStatement: string, reason: string) => {
    const reasonKey: string = reason.toLowerCase();
    const fileKey: string = filePath.toLowerCase();
    const stmtKey: string = sqlStatement.toLowerCase();
    const migrUUID: string = migrationUUID ?? "";
    const newAcknowledgedState =
      acknowledgedObjects?.[migrUUID]?.[fileKey]?.[stmtKey]?.[reasonKey] ?? false;
    setLocalAcknowledgedObjects((prev) => ({
      ...prev,
      [migrUUID]: {
        ...(prev[migrUUID] || {}),
        [fileKey]: {
          ...(prev[migrUUID]?.[fileKey] || {}),
          [stmtKey]: {
            ...(prev[migrUUID]?.[fileKey]?.[stmtKey] || {}),
            [reasonKey]: newAcknowledgedState,
          },
        },
      },
    }));

    if (toggleAcknowledgment) {
      toggleAcknowledgment(filePath, sqlStatement, reason);
    }
  };

  const mappedData = getMappedData(data?.issues);
  const [searchByItem, setSearchByItem] = React.useState<string>("Any");

  const filteredData: typeof mappedData = useMemo(() => {
    const searchQuery = search.toLowerCase().trim();

    const isSearchMatched = (query: string, fields: string[]): boolean => {
      if (query.length === 0) {
        return true;
      }
      for (const field of fields) {
        if (field.toLowerCase().includes(query)) {
          return true;
        }
      }
      return false;
    };

    const filterItems = (item: (typeof mappedData)[0], ackType: string | "All") => {
      const sqlStatements: string[] = [];
      const reasons: string[] = [];
      const issueTypes: string[] = [];
      const suggestions: string[] = [];
      const GHs: string[] = [];
      const docs_links: string[] = [];
      const minimumVersionsFixedIn: string[] = [];

      item.sqlStatements.forEach((stmt, index) => {
        const fileKey: string = item.groupKey.toLowerCase();
        const sqlKey: string = stmt.toLowerCase();
        const reasonKey: string = item.reasons[index].toLowerCase();

        const isAcknowledged =
          acknowledgedObjects?.[migrationUUID ?? ""]?.[fileKey]?.[sqlKey]?.[reasonKey];

        const ackMatched: boolean =
          ackType === "All" ||
          (ackType === "Acknowledged" && isAcknowledged === true) ||
          (ackType === "Not acknowledged" && !isAcknowledged);

        const getSearchFields = (
          searchByItem: string,
          item: {
            groupKey: string;
            sqlStatements: string[];
            reasons: string[];
            issueTypes: string[];
            GHs: string[];
            suggestions: string[];
            docs_links: string[];
            minimumVersionsFixedIn: string[];
          },
          index: number,
          stmt: string
        ): string[] => {
          switch (searchByItem) {
            case "Any":
              return [
                stmt,
                item.reasons[index],
                formatSnakeCase(item.issueTypes[index]),
                item.groupKey,
                item.minimumVersionsFixedIn[index]
              ];
            case "Issue type":
              return [formatSnakeCase(item.issueTypes[index])];
            case "SQL Statement":
              return [stmt];
            case "File Name (Searches using)":
              return [item.groupKey];
            case "Reason":
              return [item.reasons[index]];
            case "Suggestion":
              return [item.suggestions[index]];
            case "Minimum Versions Fixed In":
              return [item.minimumVersionsFixedIn[index]];
            default:
              return [];
          }
        };

        const searchFields: string[] = getSearchFields(searchByItem, item, index, stmt);
        const isIssueFilterTypeMatched: boolean =
          selectedIssueTypeFilter === "Any" ||
          formatSnakeCase(item.issueTypes[index]).toLowerCase() ===
          selectedIssueTypeFilter.toLowerCase();

        if (ackMatched && isIssueFilterTypeMatched && isSearchMatched(searchQuery, searchFields)) {
          sqlStatements.push(stmt);
          reasons.push(item.reasons[index]);
          issueTypes.push(item.issueTypes[index]);
          suggestions.push(item.suggestions[index]);
          GHs.push(item.GHs[index]);
          docs_links.push(item.docs_links[index]);
          minimumVersionsFixedIn.push(item.minimumVersionsFixedIn[index]);
        }
      });

      return sqlStatements.length > 0 && reasons.length > 0 && issueTypes.length > 0
        ? {
          groupKey: item.groupKey,
          sqlStatements,
          reasons,
          issueTypes,
          suggestions,
          GHs,
          docs_links,
          minimumVersionsFixedIn
        }
        : null;
    };

    return mappedData.reduce((result, item) => {
      const filteredItem = filterItems(item, selectedAck);
      if (filteredItem) result.push(filteredItem);
      return result;
    }, [] as typeof mappedData);
  }, [search, selectedAck, searchByItem, mappedData, selectedIssueTypeFilter, page]);

  const filteredPaginatedData = useMemo(() => {
    const startIndex = page * perPage;
    const endIndex = startIndex + perPage;

    // Tracks number of SQL statements counted
    let count = 0;

    // Paginate based on SQL statements
    let paginatedData: typeof filteredData = [];

    for (let i = 0; i < filteredData.length; i++) {
      let tempSqlStatements: string[] = [];
      let tempReasons: string[] = [];
      let tempIssueTypes: string[] = [];
      let tempSuggestions: string[] = [];
      let tempGHs: string[] = [];
      let tempDocsLinks: string[] = [];
      let tempMinimumVersionsFixedIn: string[] = [];

      for (let j = 0; j < filteredData[i].sqlStatements.length; j++) {
        if (count >= endIndex) break;
        if (count >= startIndex) {
          tempSqlStatements.push(filteredData[i].sqlStatements[j]);
          tempReasons.push(filteredData[i].reasons[j]);
          tempIssueTypes.push(filteredData[i].issueTypes[j]);
          tempSuggestions.push(filteredData[i].suggestions[j]);
          tempGHs.push(filteredData[i].GHs[j]);
          tempDocsLinks.push(filteredData[i].docs_links[j]);
          tempMinimumVersionsFixedIn.push(filteredData[i].minimumVersionsFixedIn[j]);
        }
        count++;
      }

      if (tempSqlStatements.length > 0) {
        paginatedData.push({
          groupKey: filteredData[i].groupKey,
          sqlStatements: tempSqlStatements,
          reasons: tempReasons,
          issueTypes: tempIssueTypes,
          suggestions: tempSuggestions,
          GHs: tempGHs,
          docs_links: tempDocsLinks,
          minimumVersionsFixedIn: tempMinimumVersionsFixedIn,
        });
      }

      if (count >= endIndex) break;
    }

    return paginatedData;
  }, [filteredData, page, perPage]);

  React.useEffect(() => {
    const totalItemCount =
      filteredData?.reduce((acc, obj) => acc + (obj.sqlStatements?.length || 0), 0) ?? 0;
    setTotalItemCount(totalItemCount);

    const count = filteredData?.reduce((acc, obj) => {
      return (
        acc +
        (obj?.sqlStatements?.filter(
          (it, index) =>
            it &&
            acknowledgedObjects?.[migrationUUID]?.[obj.groupKey.toLowerCase()]?.[
              it.toLowerCase()
            ]?.[obj.reasons?.[index].toLowerCase()] === true
        ).length || 0)
      );
    }, 0);

    setCurrValue(count);
  }, [mappedData]);

  const filterIssueTypes = useMemo(() => {
    const issueTypeSet = new Set<string>();
    for (const item of mappedData) {
      for (const issue of item.issueTypes) {
        issueTypeSet.add(formatSnakeCase(issue));
      }
    }

    return Array.from(issueTypeSet);
  }, [mappedData]);

  return (
    <YBModal
      open={!!data}
      title={header + (data?.objectType ? `: ${data.objectType}` : "")}
      onClose={() => {
        setPage(0);
        setSelectedAck("All");
        setSearch("");
        setSearchByItem("Any");
        setSelectedIssueTypeFilter("Any");
        onClose();
      }}
      enableBackdropDismiss
      titleSeparator
      cancelLabel={t("common.close")}
      isSidePanel
    >
      <Box display="flex" alignItems="center" gridGap={10} my={2}>
        <Box flex={0.75}>
          <Typography variant="body1" className={classes.label}>
            {t("clusterDetail.voyager.planAndAssess.refactoring.details.searchBy")}
          </Typography>
          <YBSelect
            className={classes.fullWidth}
            value={searchByItem}
            onChange={(e) => {
              setSearchByItem(e.target.value as string);
              setPage(0);
            }}
          >
            <MenuItem value="Any">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.any")}
            </MenuItem>
            <Divider className={classes.divider} />
            <MenuItem value="Issue type">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.issueType")}
            </MenuItem>
            <MenuItem value="SQL Statement">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.sqlStatement")}
            </MenuItem>
            <MenuItem value="File Name (Searches using)">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.fileName")}
            </MenuItem>
            <MenuItem value="Reason">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.reason")}
            </MenuItem>
            <MenuItem value="Suggestion">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.suggestion")}
            </MenuItem>
            <MenuItem value="Minimum Versions Fixed In">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.minVersionsFixedIn")}
            </MenuItem>
          </YBSelect>
        </Box>
        <Box flex={1.25}>
          <Typography variant="body1" className={classes.label}>
            {t("clusterDetail.voyager.planAndAssess.refactoring.details.filterBy")}
          </Typography>
          <YBSelect
            className={classes.fullWidth}
            value={selectedIssueTypeFilter}
            onChange={(e) => setSelectedIssueTypeFilter(e.target.value as string)}
          >
            <MenuItem value="Any">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.any")}
            </MenuItem>
            <Divider className={classes.divider} />
            {filterIssueTypes.map((currentIssue) => (
              <MenuItem value={currentIssue}>{currentIssue}</MenuItem>
            ))}
          </YBSelect>
        </Box>
        <Box flex={3}>
          <Typography variant="body1" className={classes.label}>
            {t("clusterDetail.voyager.planAndAssess.refactoring.details.search")}
          </Typography>
          <YBInput
            className={classes.fullWidth}
            placeholder={t(
              "clusterDetail.voyager.planAndAssess.refactoring.details.searchPlaceholder"
            )}
            InputProps={{
              startAdornment: <SearchIcon />,
            }}
            onChange={(ev) => setSearch(ev.target.value as string)}
            value={search}
          />
        </Box>
        <Box flex={1.25}>
          <Typography variant="body1" className={classes.label}>
            {t("clusterDetail.voyager.planAndAssess.refactoring.details.acknowledged")}
          </Typography>
          <YBSelect
            className={classes.fullWidth}
            value={selectedAck}
            onChange={(e) => setSelectedAck(e.target.value as string)}
          >
            <MenuItem value="All">{t("common.all")}</MenuItem>
            <Divider className={classes.divider} />
            <MenuItem value="Acknowledged">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.acknowledged")}
            </MenuItem>
            <MenuItem value="Not acknowledged">
              {t("clusterDetail.voyager.planAndAssess.refactoring.details.notAcknowledged")}
            </MenuItem>
          </YBSelect>
        </Box>
      </Box>

      <Box>
        <YBAccordion
          titleContent={title}
          renderChips={() => (
            <YBBadge
              icon={false}
              text={`${currValue} / ${totalItemCount} ${title}`}
              variant={BadgeVariant.Light}
              className={classes.assessmentBadge}
            />
          )}
          graySummaryBg
          defaultExpanded
        >
          <Box display="flex" flexDirection="column" gridGap={10}>
            {filteredPaginatedData?.map(({
              groupKey,
              sqlStatements,
              reasons,
              issueTypes,
              GHs,
              docs_links,
              suggestions,
              minimumVersionsFixedIn
            }) => (
              <React.Fragment key={groupKey}>
                <Box display="flex" flexDirection="column">
                  <Box my={2}>
                    <Typography variant="body2">{groupKey}</Typography>
                  </Box>
                  <Box display="flex" flexDirection="column" gridGap={10}>
                    {sqlStatements.map((sql, index) => {
                      const tableData = [
                        {
                          key: t("clusterDetail.voyager.planAndAssess.refactoring.details.reason"),
                          value: reasons[index] ? (
                            <Box
                              display="flex"
                              alignItems="center"
                              gridGap={6}
                              className={classes.warningBox}
                            >
                              <WarningIcon className={classes.icon} />
                              {reasons[index]}
                            </Box>
                          ) : "N/A",
                        },
                        {
                          key: t("clusterDetail.voyager.planAndAssess.refactoring.details."
                            + "issueType"),
                          value: issueTypes[index] ? formatSnakeCase(issueTypes[index]) : "N/A",
                        },
                        {
                          key: t("clusterDetail.voyager.planAndAssess.refactoring.details." +
                            "suggestion"),
                          value: suggestions[index] || "N/A",
                        },
                        {
                          key: t("clusterDetail.voyager.planAndAssess.refactoring.details."
                            +"minVersionsFixedIn"),
                          value: minimumVersionsFixedIn[index] || "N/A",
                        },
                      ];

                      const columns: any[] = [
                        {
                          name: "key",
                          label: t("clusterDetail.voyager.planAndAssess.refactoring.details.key"),
                          options: {
                            setCellHeaderProps: () => ({ style:
                              { padding: "8px 16px", textAlign: "center" } }),
                            setCellProps: () => ({ style:
                              { padding: "8px 16px", textAlign: "center" } }),
                            customBodyRender: (value: string) => (
                              <Typography variant="body2" className={classes.label}>
                                {value}
                              </Typography>
                            ),
                          },
                        },
                        {
                          name: "value",
                          label: t("clusterDetail.voyager.planAndAssess.refactoring.details.value"),
                          options: {
                            setCellHeaderProps: () => ({ style:
                              { padding: "8px 16px", textAlign: "center" } }),
                            setCellProps: () => ({ style:
                              { padding: "8px 16px", textAlign: "center" } }),
                            customBodyRender: (value: string) => (
                              <Box display="flex" justifyContent="center"
                                alignItems="center" sx={{ fontSize: "12px" }}>
                                {value}
                              </Box>
                            ),
                          },
                        },
                      ];

                      return (
                        <React.Fragment key={index}>
                          {index > 0 && (
                            <>
                              <Divider className={classes.dividerNew} />
                            </>
                          )}
                          <Box display="flex" flexDirection="column" gridGap={2}>
                            <YBToggle
                              className={classes.toggleSwitch}
                              label={t("clusterDetail.voyager.planAndAssess.refactoring."
                                +"details.acknowledge")}
                              checked={
                                localAcknowledgedObjects?.[migrationUUID ?? ""]?.
                                  [groupKey.toLowerCase()]?.[
                                  sql.toLowerCase()
                                ]?.[reasons[index]?.toLowerCase()] ?? false
                              }
                              onChange={() => handleLocalToggle(groupKey, sql, reasons[index])}
                            />
                            <Box className={classes.sqlSection}>
                              <YBCodeBlock
                                text={(sql)}
                                preClassName={classes.queryCodeBlock}
                              />
                            </Box>
                            <YBTable
                              title=""
                              data={tableData}
                              columns={columns}
                              options={{
                                selectableRows: "none",
                                pagination: false,
                                setTableProps: () => ({ size: "small" }),
                              }}
                              withBorder
                              cellBorder
                              hideHeader
                            />
                            <Box className={classes.linkContainer}>
                              {GHs[index] && (
                                <Link href={GHs[index]} target="_blank"
                                  className={classes.linkItem}>
                                  <GithubIcon className={classes.githubIcon} />
                                  <Typography variant="body2">
                                    {t("clusterDetail.voyager.planAndAssess.recommendation."
                                      + "schemaChanges.linkToGithub")}
                                  </Typography>
                                  <ExternalLink />
                                </Link>
                              )}
                              {docs_links[index] && (
                                <Link href={docs_links[index]} target="_blank"
                                  className={classes.linkItem}>
                                  <YBLogo />
                                  <Typography variant="body2">
                                    {t("clusterDetail.voyager.planAndAssess.recommendation."
                                      + "schemaChanges.linkToDocs")}
                                  </Typography>
                                  <ExternalLink />
                                </Link>
                              )}
                            </Box>
                          </Box>
                        </React.Fragment>
                      );
                    })}
                  </Box>
                </Box>
              </React.Fragment>
            ))}
            <Box ml="auto">
              <TablePagination
                component="div"
                count={filteredData?.reduce((acc, obj) => acc + obj.sqlStatements.length, 0) || 0}
                page={page}
                onPageChange={(_, newPage) => setPage(newPage)}
                rowsPerPageOptions={[5, 10, 20]}
                rowsPerPage={perPage}
                onRowsPerPageChange={(e) => setPerPage(parseInt(e.target.value, 10))}
              />
            </Box>
          </Box>
        </YBAccordion>
      </Box>
    </YBModal>
  );
};
