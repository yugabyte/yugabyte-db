import React, { FC, useMemo } from "react";
import {
  Box,
  Divider,
  MenuItem,
  TablePagination,
  Typography,
  makeStyles,
  Paper,
  Grid
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBInput, YBModal, YBSelect, YBToggle } from "@app/components";
import { AlertVariant } from "@app/components";
import SearchIcon from "@app/assets/search.svg";
import ExternalLink from "@app/assets/book.svg"
import type { SqlObjectsDetails } from "@app/api/src";
import { getMappedData } from "./refactoringUtils";
import {
  formatSnakeCase,
  useQueryParams,
  useToast,
  copyToClipboard,
} from "@app/helpers";
import { Link } from "@material-ui/core";
import GithubMarkIcon from "@app/assets/github-mark.png";
import WarningIcon from "@app/assets/warning_triangle.svg";
import { MetadataItem } from "../../components/MetadataItem";
import CopyIconBlue from "@app/assets/copyIconBlue.svg";
import BulletCircle from "@app/assets/bullet-circle.svg";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left",
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
    margin: theme.spacing(1, 0),
  },
  toggleSwitch: {
    flexShrink: 0,
  },
  icon: {
    height: theme.spacing(1.75),
    width: theme.spacing(1.75),
    marginBottom: theme.spacing(0.25),
    color: theme.palette.warning[500],
  },
  linkIcon: {
    width: theme.spacing(3),
    height: theme.spacing(3),
    display: 'inline-block',
    verticalAlign: 'middle'
  },
  filePathContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),
    maxWidth: '100%'
  },
  filePathSpan: {
    whiteSpace: 'pre-wrap',
    wordBreak: 'break-word',
    overflowWrap: 'anywhere'
  },
  copyButton: {
    display: 'inline-flex',
    alignItems: 'center',
    justifyContent: 'center',
    cursor: 'pointer',
    padding: theme.spacing(0.25),
    borderRadius: theme.shape.borderRadius,
    '&:hover': {
      backgroundColor: theme.palette.action.hover
    }
  },
  copyIcon: {
    width: theme.spacing(3),
    height: theme.spacing(3),
    flexShrink: 0,
    verticalAlign: 'middle'
  },
  acknowledgmentContainer: {
    display: 'flex',
    padding: theme.spacing(0.5, 0.75),
    justifyContent: 'center',
    alignItems: 'center',
    gap: theme.spacing(0.5),
    borderRadius: theme.shape.borderRadius,
    background: theme.palette.primary[100],
    color: theme.palette.primary[600],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.pxToRem(11.5),
    fontStyle: 'normal',
    fontWeight: theme.typography.fontWeightRegular as number,
    lineHeight: '16px'
  },
  issueCard: {
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: theme.shape.borderRadius,
    padding: theme.spacing(2),
    backgroundColor: theme.palette.background.paper,
    width: '100%'
  },
  issueTitle: {
    fontWeight: theme.typography.fontWeightBold as number,
    fontSize: theme.typography.pxToRem(16)
  },
  sqlCodeBlock: {
    display: 'flex',
    padding: theme.spacing(2),
    alignItems: 'flex-start',
    gap: theme.spacing(1.25),
    flex: '1 0 0',
    width: '100%',
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.divider}`,
    background: theme.palette.grey[100],
    position: 'relative'
  },
  copyIconClickable: {
    width: theme.spacing(3),
    height: theme.spacing(3),
    cursor: 'pointer',
    opacity: 0.75,
    transition: 'opacity 120ms ease',
    '&:hover': {
      opacity: 1
    }
  },
  sqlText: {
    wordBreak: 'break-word',
    overflowWrap: 'anywhere',
    whiteSpace: 'pre-wrap',
    color: theme.palette.text.primary,
    fontFamily: 'Menlo',
    fontSize: theme.typography.pxToRem(13),
    fontStyle: 'normal',
    fontWeight: theme.typography.fontWeightRegular as number,
    lineHeight: '200%',
    flex: '1 0 0'
  },
  suggestionContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),
    alignSelf: 'stretch',
    position: 'relative',
    width: '100%'
  },
  suggestionRow: {
    display: 'flex',
    alignItems: 'flex-start',
    gap: theme.spacing(2),
    alignSelf: 'stretch',
    position: 'relative'
  },
  bulletContainer: {
    width: theme.spacing(5),
    height: theme.spacing(2.5),
    flexShrink: 0,
    position: 'relative'
  },
  bulletIcon: {
    position: 'absolute',
    left: theme.spacing(1),
    top: theme.spacing(-0.5)
  },
  suggestionContent: {
    display: 'flex',
    alignItems: 'flex-start',
    flex: 1
  },
  suggestionLabel: {
    minWidth: '200px'
  },
  suggestionText: {
    lineHeight: '20px'
  },
  suggestionTextWithIcon: {
    lineHeight: '20px',
    display: 'inline-flex',
    alignItems: 'center',
    gap: theme.spacing(0.5)
  },
  warningIcon: {
    width: theme.spacing(2.5),
    height: theme.spacing(2.5),
    flexShrink: 0
  },
  linkButton: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: theme.spacing(1),
    verticalAlign: 'middle',
    textDecoration: 'none',
    color: theme.palette.primary.main
  },
  flexContainer: {
    display: 'flex',
    alignItems: 'center'
  },
  spaceBetween: {
    justifyContent: 'space-between'
  },
  justifyEnd: {
    justifyContent: 'flex-end'
  },
  flexColumn: {
    flexDirection: 'column'
  },
  gap2: {
    gap: theme.spacing(2)
  },
  my2: {
    margin: theme.spacing(2, 0)
  },
  mlAuto: {
    marginLeft: 'auto'
  },
  p2: {
    padding: theme.spacing(2)
  },
  mb2: {
    marginBottom: theme.spacing(2)
  },
  mb3: {
    marginBottom: theme.spacing(3)
  }
}));

interface MigrationRefactoringSidePanelProps {
  data: SqlObjectsDetails | undefined;
  onClose: () => void;
  header: string;
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
  acknowledgedObjects,
  toggleAcknowledgment,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const { addToast } = useToast();

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

  // Function to copy file path
  const copyFilePath = async (filePath: string) =>
    copyToClipboard(filePath, {
      onSuccess: () => addToast(AlertVariant.Success, t('common.copyCodeSuccess'), 3000),
      onError: (msg?: string) =>
        addToast(AlertVariant.Error, msg || 'Failed to copy to clipboard', 5000)
    });

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
      title={header}
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
      <Box className={classes.my2}>
        <Paper>
          <Box className={`${classes.p2} ${classes.grayBg} ${classes.flexContainer}`}>
            <Grid container spacing={2} alignItems="flex-start">
              {data?.objectType && (
                <Grid item xs={12} sm={3}>
                  <MetadataItem
                    layout="vertical"
                    label={t(
                      "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.objectType"
                    )}
                    value={data.objectType}
                  />
                </Grid>
              )}

              {mappedData.length > 0 && mappedData[0]?.groupKey && (
                <Grid item xs={12} sm={6}>
                  <Box style={{ marginTop: '-4px' }}>
                    <MetadataItem
                      layout="vertical"
                      label={t("clusterDetail.voyager.planAndAssess.refactoring.fileDirectory")}
                      value={
                          <Box className={classes.filePathContainer}>
                          <span className={classes.filePathSpan}>
                            {(mappedData[0].groupKey)}
                          </span>
                          <Box
                            className={classes.copyButton}
                            onClick={() => copyFilePath(mappedData[0].groupKey)}
                            title={`Click to copy: ${mappedData[0].groupKey}`}
                          >
                            <CopyIconBlue className={classes.copyIcon} />
                          </Box>
                        </Box>
                      }
                    />
                  </Box>
                </Grid>
              )}

               <Grid item xs={12} sm={3}>
                <MetadataItem
                  layout="vertical"
                  label={t("clusterDetail.voyager.planAndAssess.refactoring.details.acknowledged")}
                  value={
                    <Box className={classes.acknowledgmentContainer}>
                      {`${currValue} / ${totalItemCount} objects`}
                    </Box>
                  }
                />
              </Grid>
            </Grid>
          </Box>
        </Paper>
      </Box>

      <Box className={`${classes.flexContainer} ${classes.gap2} ${classes.my2}`}>
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

      <Box className={`${classes.flexContainer} ${classes.flexColumn} ${classes.gap2}`}>
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
            {sqlStatements.map((sql, index) => (
              <Box
                key={`${groupKey}-${index}`}
                className={classes.issueCard}
              >
                {/* Header with Issue title and Acknowledge toggle */}
                <Box className={`${classes.flexContainer} ${classes.spaceBetween} ${classes.mb2}`}>
                  <Typography variant="h6" className={classes.issueTitle}>
                    Issue
                  </Typography>
                  <YBToggle
                    className={classes.toggleSwitch}
                    label={t("clusterDetail.voyager.planAndAssess.refactoring.details.acknowledge")}
                    checked={
                      localAcknowledgedObjects?.[migrationUUID ?? ""]?.
                      [groupKey.toLowerCase()]?.[
                      sql.toLowerCase()
                      ]?.[reasons[index]?.toLowerCase()] ?? false
                    }
                    onChange={() => handleLocalToggle(groupKey, sql, reasons[index])}
                  />
                </Box>

                {/* SQL Code Block with copy icon */}
                <Box className={classes.sqlCodeBlock} mb={3}>
                  <Box className={classes.sqlText}>{sql}</Box>
                  <Box
                    className={classes.copyButton}
                    onClick={() => copyFilePath(sql)}
                  >
                    <CopyIconBlue className={classes.copyIconClickable} />
                  </Box>
                </Box>

                {/* Details Grid */}
                <Box className={`${classes.suggestionContainer} ${classes.mb3}`}>
                  {/* Reason */}
                  <Box className={classes.suggestionRow}>
                    <Box className={classes.bulletContainer}>
                      <BulletCircle className={classes.bulletIcon} />
                    </Box>
                    <Box className={classes.suggestionContent}>
                      <Typography className={`${classes.label} ${classes.suggestionLabel}`}>
                        {t("clusterDetail.voyager.planAndAssess.refactoring.details.reason")}
                      </Typography>
                      <Typography variant="body2" className={classes.suggestionTextWithIcon}>
                        {reasons[index] || "N/A"}
                        <WarningIcon className={classes.warningIcon} />
                      </Typography>
                    </Box>
                  </Box>

                  {/* Issue Type */}
                  <Box className={classes.suggestionRow}>
                    <Box className={classes.bulletContainer}>
                      <BulletCircle className={classes.bulletIcon} />
                    </Box>
                    <Box className={classes.suggestionContent}>
                      <Typography className={`${classes.label} ${classes.suggestionLabel}`}>
                        {t("clusterDetail.voyager.planAndAssess.refactoring.details.issueType")}
                      </Typography>
                      <Typography variant="body2" className={classes.suggestionText}>
                        {issueTypes[index] ? formatSnakeCase(issueTypes[index]) : "N/A"}
                      </Typography>
                    </Box>
                  </Box>

                  {/* Suggestion */}
                  <Box className={classes.suggestionRow}>
                    <Box className={classes.bulletContainer}>
                      <BulletCircle className={classes.bulletIcon} />
                    </Box>
                    <Box className={classes.suggestionContent}>
                      <Typography className={`${classes.label} ${classes.suggestionLabel}`}>
                        {t("clusterDetail.voyager.planAndAssess.refactoring.details.suggestion")}
                      </Typography>
                      <Typography variant="body2" className={classes.suggestionText}>
                        {suggestions[index] || "N/A"}
                      </Typography>
                    </Box>
                  </Box>

                  {/* Minimum Version Fixed In */}
                  <Box className={classes.suggestionRow}>
                    <Box className={classes.bulletContainer}>
                      <BulletCircle className={classes.bulletIcon} />
                    </Box>
                    <Box className={classes.suggestionContent}>
                      <Typography className={`${classes.label} ${classes.suggestionLabel}`}>
                        {t(
                          "clusterDetail.voyager.planAndAssess.refactoring.details." +
                          "minVersionsFixedIn"
                        )}
                      </Typography>
                      <Typography variant="body2" className={classes.suggestionText}>
                        {minimumVersionsFixedIn[index] || "N/A"}
                      </Typography>
                    </Box>
                  </Box>
                </Box>

                {/* Links */}
                <Box
                  className={`${classes.flexContainer} ${classes.justifyEnd} ${classes.gap2}`}
                  style={{ alignItems: 'center' }}
                >
                  {GHs[index] && (
                    <Link
                      href={GHs[index]}
                      target="_blank"
                      className={classes.linkButton}
                    >
                      <img
                        src={GithubMarkIcon}
                        alt="GitHub"
                        className={classes.linkIcon}
                      />
                      <Typography variant="body2">Github Issue</Typography>
                    </Link>
                  )}
                  {docs_links[index] && (
                    <Link
                      href={docs_links[index]}
                      target="_blank"
                      className={classes.linkButton}
                    >
                      <ExternalLink className={classes.linkIcon} />
                      <Typography variant="body2">Learn more</Typography>
                    </Link>
                  )}
                </Box>
              </Box>
            ))}
          </React.Fragment>
        ))}

        {/* Pagination */}
        <Box className={classes.mlAuto}>
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
    </YBModal>
  );
};
