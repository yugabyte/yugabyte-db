import React, { FC, useMemo, Fragment } from "react";
import type { RefactoringCount } from "@app/api/src";
import { useTranslation } from "react-i18next";
import { YBTable, YBButton, YBTooltip } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { AlertVariant } from "@app/components";
import {
  Box,
  TableCell,
  TableRow,
  makeStyles,
  Typography,
  Theme,
  Collapse,
  useTheme
} from "@material-ui/core";
import type { SqlObjectsDetails, AnalysisIssueDetails } from "@app/api/src";
import MinusIcon from "@app/assets/minus_icon.svg";
import PlusIcon from "@app/assets/plus_icon_24.svg";
import ArrowRightIcon from "@app/assets/arrow-right.svg";
import InnerTableDivider from "@app/assets/inner_table_divider.svg";
import CaretRightIconBlue from "@app/assets/caretRightIconBlue.svg";
import CopyIconBlue from "@app/assets/copyIconBlue.svg";
import ExpandIcon from "@app/assets/expand.svg";
import CollapseIcon from "@app/assets/collapse.svg";
import {
  capitalizeFirstLetter,
  formatSnakeCase,
  useQueryParams,
  useToast,
  truncateString,
  copyToClipboard,
} from "@app/helpers";
import { MigrationRefactoringSidePanel } from "../assessment/AssessmentRefactoringSidePanel";
interface RefactoringGraphProps {
  sqlObjects: RefactoringCount[] | undefined;
  sqlObjectsList?: SqlObjectsDetails[] | undefined;
  isAssessmentPage?: boolean | undefined;
  setSelectedObjectType?: React.Dispatch<React.SetStateAction<string>> | undefined;
  scrollToIssueTable?: () => void;
  objectTypeSelectRef?: React.RefObject<HTMLInputElement>;
}

type AID = AnalysisIssueDetails;
interface FileIssue {
  objectType: AID["issueType"];
  filePath: AID["filePath"];
  objectName: AID["objectName"];
  acknowledgedObjects: {
    ackCount: number;
    totalCount: number;
  };
}

const useStyles = makeStyles((theme: Theme) => ({
  innerTable: {
    borderRadius: theme.shape.borderRadius,
    backgroundColor: theme.palette.background.default,
    marginLeft: theme.spacing(5),
    marginRight: theme.spacing(2),
    padding: theme.spacing(1),
    marginBottom: theme.spacing(1),
    "& .MuiPaper-root": {
      backgroundColor: "transparent !important",
    },
    "& .MuiTable-root": {
      tableLayout: "auto !important",
      "& .MuiTableBody-root .MuiTableRow-root:last-child .MuiTableCell-root": {
        borderBottom: `1px solid ${theme.palette.divider}`,
      },
    },
    "& .MuiTableCell-root": {
      overflow: "visible !important",
      minHeight: "50px !important",
    },

  },
  innerTableParent: {
    padding: theme.spacing(0.5, 0, 1, 0),
    position: "relative",
  },
  seeDetailsLink: {
    color: theme.palette.primary.main,
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.pxToRem(11.5),
    fontStyle: 'normal',
    fontWeight: theme.typography.fontWeightRegular as number,
    lineHeight: '32px',
    marginLeft: theme.spacing(2),
    cursor: 'pointer',
    textDecoration: 'none',
    '&:hover': {
      textDecoration: 'underline',
    },
  },
  seeDetailsHyphen: {
    color: theme.palette.text.disabled,
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.pxToRem(11.5),
    fontStyle: 'normal',
    fontWeight: theme.typography.fontWeightRegular as number,
    lineHeight: '32px',
    cursor: 'default',
    textDecoration: 'none',
  },
  tableWrapper: {
    marginTop: theme.spacing(4),
    marginBottom: theme.spacing(7),
    "& .MuiTableRow-root": {
      height: "32px !important",
    },
    "& .MuiTableCell-root": {
      height: "32px !important",
      padding: "0 !important",
      lineHeight: "32px !important",
      borderBottom: `1px solid ${theme.palette.divider}`
    },
    "& .MuiTableHead-root + .MuiTableBody-root": {
      marginTop: theme.spacing(1)
    },
    // Collapsible row must not reserve space when collapsed
    "& $collapsibleRow": {
      height: "auto !important",
    },
    "& $collapsibleCell": {
      height: "auto !important",
      padding: "0 !important",
      lineHeight: "normal !important",
      borderBottom: "none !important",
    }
  },
  accordionContent: {
    padding: 0,
    "& .MuiAccordionDetails-root": {
      padding: 0
    }
  },
  innerTableWithDividers: {
    position: "relative",
  },
  cellHeader: {
    width: '40px',
    padding: 0,
    textAlign: 'center',
    verticalAlign: 'middle',
  },
  badgeTextStyle: {
    minWidth: '16px',
    display: 'inline-block',
    textAlign: 'center',
    fontSize: `${theme.typography.pxToRem(11.5)} !important`,
    fontFamily: theme.typography.fontFamily,
    fontStyle: 'normal',
    fontWeight: theme.typography.fontWeightRegular as any,
    lineHeight: '16px'
  },
  viewDetailsLink: {
    color: theme.palette.primary[600],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.pxToRem(11.5),
    fontStyle: 'normal',
    fontWeight: theme.typography.fontWeightRegular as any,
    lineHeight: 'normal',
    cursor: 'pointer',
    textDecoration: 'none',
    marginLeft: theme.spacing(1),
    '&:hover': {
      textDecoration: 'underline',
    },
  },
  filePathText: {
    color: theme.palette.grey[900],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.pxToRem(13),
    fontWeight: theme.typography.fontWeightRegular as any,
    lineHeight: '20px',
    display: 'inline-flex',
    alignItems: 'center',
    gap: theme.spacing(0.5)
  },
  copyButtonBox: {
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
  copyIconStyle: {
    width: '24px',
    height: '24px',
    flexShrink: 0
  },
  expandedRowTitle: {
    marginBottom: theme.spacing(2),
    color: theme.palette.grey[900],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.pxToRem(13),
    fontStyle: 'normal',
    fontWeight: theme.typography.fontWeightBold as any,
    lineHeight: '16px',
    display: 'flex',
    alignItems: 'center',
    marginTop: theme.spacing(2),
    gap: theme.spacing(1),
    backgroundColor: theme.palette.background.paper,
    paddingLeft: theme.spacing(6)
  },
  arrowIcon: {
    width: '24px',
    height: '24px'
  },
  expandedContent: {
    display: 'flex',
    alignItems: 'flex-start'
  },
  dividerContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: 0,
    marginRight: theme.spacing(1.25),
    marginLeft: theme.spacing(1),
    marginTop: theme.spacing(4.75)
  },
  dividerRow: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'flex-start',
    paddingTop: 0,
    marginBottom: theme.spacing(1.375),
    marginLeft: theme.spacing(2),
    marginRight: theme.spacing(2)
  },
  tableContainer: {
    flex: 1,
    marginBottom: theme.spacing(1.25)
  },
  expansionButton: {
    cursor: "pointer",
    height: "32px",
    width: "40px",
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    margin: 0,
    padding: theme.spacing(0, 1)
  },
  objectTypeText: {
    color: theme.palette.grey[900],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.pxToRem(13),
    fontStyle: 'normal',
    fontWeight: theme.typography.fontWeightRegular as any,
    lineHeight: '32px',
  },
  rightArrowButton: {
    cursor: "pointer",
    height: "32px",
    width: "50px",
    display: "flex",
    alignItems: "center",
    justifyContent: "center"
  },
  outerTableRow: {
    height: "40px !important",
    "& .MuiTableCell-root": {
      height: "40px !important",
      padding: theme.spacing(1, 3.75),
    }
  },
  cellWithPadding: {
    padding: theme.spacing(1, 0)
  },
  cellWithPaddingManual: {
    padding: theme.spacing(1, 3.125)
  },
  cellWithPaddingRight: {
    padding: theme.spacing(1, 1)
  },
  flexContainer: {
    display: "flex",
    flexDirection: "row",
    alignItems: "center",
  },
  expandCollapseButtonContainer: {
    position: "absolute",
    right: 0,
    top: 0
  },
  collapsibleRow: {
    height: 'auto !important',
  },
  collapsibleCell: {
    padding: '0 !important',
    border: 'none !important',
    height: 'auto !important',
  }
}));

const ACKNOWLEDGED_OBJECTS_LOCAL_STORAGE_KEY: string = "acknowledgedObjects";

export const RefactoringGraph: FC<RefactoringGraphProps> = ({
  sqlObjects,
  sqlObjectsList,
  isAssessmentPage,
  setSelectedObjectType,
  scrollToIssueTable,
  objectTypeSelectRef
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const theme = useTheme();
  const queryParams = useQueryParams();
  const { addToast } = useToast();
  const migrationUUID: string = queryParams.get("migration_uuid") ?? "";


  // Function to copy file path (reused util)
  const copyFilePath = async (filePath: string) =>
    copyToClipboard(filePath, {
      onSuccess: () => addToast(AlertVariant.Success, t('common.copyCodeSuccess'), 3000),
      onError: (msg?: string) =>
        addToast(AlertVariant.Error, msg || 'Failed to copy to clipboard', 5000)
    });

  // migrationUUID not in lowerCase, filePath in lowerCase, sql in lowerCase, reason in lowerCase
  const [acknowledgedObjects, setAcknowledgedObjects] = React.useState<{
    [key: string]: {
      [key: string]: {
        [key: string]: {
          [key: string]: boolean;
        };
      };
    };
  }>({});

  const [expandedSuggestions, setExpandedSuggestions] =
    React.useState<{ [key: number]: boolean }>({});
  const [paginationState, setPaginationState] = React.useState<{
    [key: string]: { currentPage: number; rowsPerPage: number }
  }>({});
  React.useEffect(() => {
    if (!sqlObjectsList) {
      return;
    }
    const savedAcknowledgedObjects = JSON.parse(
      localStorage.getItem(ACKNOWLEDGED_OBJECTS_LOCAL_STORAGE_KEY) || "{}"
    );

    const newAcknowledgedObjects: {
      [key: string]: {
        [key: string]: {
          [key: string]: {
            [key: string]: boolean;
          };
        };
      };
    } = { ...savedAcknowledgedObjects };
    setAcknowledgedObjects(newAcknowledgedObjects);

    localStorage.setItem(
      ACKNOWLEDGED_OBJECTS_LOCAL_STORAGE_KEY,
      JSON.stringify(newAcknowledgedObjects)
    );
  }, [sqlObjectsList]);

  const [selectedDataType, setSelectedDataType] =
    React.useState<SqlObjectsDetails | undefined>(undefined);

  const [sidePanelHeader, setSidePanelHeader] = React.useState<string>("");

  const toggleAcknowledgment = (filePath: string, sqlStatement: string, reason: string) => {
    const updatedAcknowledgedObjects = { ...acknowledgedObjects };
    const filePathKey: string = filePath.toLowerCase();
    const sqlStatementKey: string = sqlStatement.toLowerCase();
    const reasonKey: string = reason.toLowerCase();
    const migUUIDVal = migrationUUID ?? "";

    if (!updatedAcknowledgedObjects[migUUIDVal]) {
      updatedAcknowledgedObjects[migUUIDVal] = {};
    }

    if (!updatedAcknowledgedObjects[migUUIDVal][filePathKey]) {
      updatedAcknowledgedObjects[migUUIDVal][filePathKey] = {};
    }

    if (!updatedAcknowledgedObjects[migUUIDVal][filePathKey][sqlStatementKey]) {
      updatedAcknowledgedObjects[migUUIDVal][filePathKey][sqlStatementKey] = {};
    }

    updatedAcknowledgedObjects[migUUIDVal][filePathKey][sqlStatementKey][reasonKey] =
      !updatedAcknowledgedObjects[migUUIDVal][filePathKey][sqlStatementKey][reasonKey];

    setAcknowledgedObjects(updatedAcknowledgedObjects);
    localStorage.setItem(
      ACKNOWLEDGED_OBJECTS_LOCAL_STORAGE_KEY,
      JSON.stringify(updatedAcknowledgedObjects)
    );
  };

  const getIssueStatus = (
    filePath: string,
    sqlStatement: string,
    reason: string,
    migrationUUID: string
  ): boolean => {
    const filePathKey = filePath.toLowerCase();
    const sqlStatementKey = sqlStatement.toLowerCase();
    const reasonKey = reason.toLowerCase();
    const uuidKey = migrationUUID ?? "";
    return acknowledgedObjects[uuidKey]?.[filePathKey]?.[sqlStatementKey]?.[reasonKey] ?? false;
  };
  const expandableGraphData: Map<string, FileIssue[]> = useMemo(() => {
    const objectTypeArrayMap = new Map<string, FileIssue[]>();
    if (!sqlObjectsList) {
      return objectTypeArrayMap;
    }
    sqlObjectsList.forEach((sqlObject) => {
      if ((sqlObject?.issues?.length ?? 0) > 0) {
        sqlObject?.issues?.forEach((error: AnalysisIssueDetails) => {

          const objectType = sqlObject?.objectType?.toLowerCase() ?? "";

          if (!objectTypeArrayMap.has(objectType)) {
            objectTypeArrayMap.set(objectType, []);
          }

          const objectName = error.objectName ?? "";
          const filePath = error?.filePath ?? "";
          const sqlStatement = error?.sqlStatement ?? "";
          const reason = error?.reason ?? "";
          const existingIssues = objectTypeArrayMap.get(objectType) ?? [];

          const existingIssueIndex = existingIssues.findIndex(
            (issue) => issue.filePath === filePath && issue.objectName === objectName
          );

          const totalCount = sqlObjectsList.reduce((count, sqlObj) => {
            const filteredErrors = sqlObj?.issues?.filter(
              (issue) => issue?.filePath === filePath && issue?.objectName === objectName
            );
            return count + (filteredErrors?.length ?? 0);
          }, 0);

          if (existingIssueIndex === -1) {
            existingIssues.push({
              objectType: sqlObject?.objectType,
              filePath: filePath,
              objectName: objectName,
              acknowledgedObjects: {
                ackCount: getIssueStatus(filePath, sqlStatement, reason, migrationUUID ?? "")
                  ? 1 : 0,
                totalCount,
              },
            });
          } else {
            const existingIssue = existingIssues[existingIssueIndex];

            if (getIssueStatus(filePath, sqlStatement, reason, migrationUUID ?? "")) {
              existingIssue.acknowledgedObjects.ackCount++;
            }
          }
        });
      }
    });

    return objectTypeArrayMap;
  }, [sqlObjectsList, acknowledgedObjects]);

  const graphData = useMemo(() => {
    if (!sqlObjects) {
      return [];
    }

    const objectTypeNameMap = new Map<string, AID["objectName"][]>();

    sqlObjectsList?.forEach((sqlObject: SqlObjectsDetails) => {
      const currentIssues: AID[] = sqlObject?.issues || [];

      currentIssues.forEach((issue: AnalysisIssueDetails) => {
        const objectType: string = sqlObject?.objectType?.toLowerCase() ?? "";
        const objectName: string = issue?.objectName?.toLowerCase() ?? "";

        if (objectTypeNameMap.has(objectType)) {
          objectTypeNameMap.get(objectType)?.push(objectName);
        } else {
          objectTypeNameMap.set(objectType, [objectName]);
        }
      });
    });

    return sqlObjects
      .filter(({ automatic, manual, invalid }) => {
        const total: number = (automatic ?? 0) + (manual ?? 0) + (invalid ?? 0);
        return total > 0;
      })
      .map(({ sql_object_type, automatic, manual, invalid }, index) => {
        const mapReturnedArray = objectTypeNameMap.get(sql_object_type!.trim().toLowerCase()) || [];
        const doesMapReturnArray: boolean = Array.isArray(mapReturnedArray);
        return {
          plusMinusExpansion: {
            mapReturnedArrayLength: doesMapReturnArray ? mapReturnedArray?.length : 0,
            index,
          },
          objectType: sql_object_type?.trim(),
          automaticDDLImport: automatic ?? 0,
          manualRefactoring: manual ?? 0,
          invalidObjCount: invalid ?? 0,
          rightArrowSidePanel: {
            mapReturnedArrayLength: doesMapReturnArray ? mapReturnedArray?.length : 0,
            sqlObjectType: sql_object_type?.trim(),
          },
        };
      });
  }, [sqlObjects]);

  if (!graphData.length) {
    return null;
  }
  const getExpandCollapseStatus = React.useMemo(() => {
    const counts = graphData?.reduce(
      (
        counts,
        {
          plusMinusExpansion,
        }: {
          plusMinusExpansion: {
            mapReturnedArrayLength: number;
            index: number;
          };
        }
      ) => {
        if (plusMinusExpansion.mapReturnedArrayLength > 0) {
          counts.totalExpandable++;
          if (expandedSuggestions[plusMinusExpansion.index]) {
            counts.totalExpanded++;
          }
        }
        return counts;
      },
      { totalExpandable: 0, totalExpanded: 0 }
    );

    return counts?.totalExpandable !== counts?.totalExpanded;
  }, [graphData, expandedSuggestions]);

  const innerColumns = [
    {
      name: "objectType",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.objectType"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(0.75) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(2) } }),
        customBodyRender: (value: string) => (
          capitalizeFirstLetter(value) || ""
        ),
      },
    },
    {
      name: "filePath",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.fileDirectory"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(0.75) } }),
        setCellProps: () => ({
          style: {
            padding: theme.spacing(2, 1, 2, 0.5),
            wordBreak: "break-word",
            overflow: "visible",
            whiteSpace: "nowrap",
            minWidth: theme.spacing(31.25)
          }
        }),
        customBodyRender: (filePath: string) => (
          <Typography className={classes.filePathText}>
            {truncateString(filePath)}
            <Box
              className={classes.copyButtonBox}
              onClick={() => copyFilePath(filePath)}
              title={`Click to copy: ${filePath}`}
            >
              <CopyIconBlue className={classes.copyIconStyle} />
            </Box>
          </Typography>
        ),
      },
    },
    {
      name: "objectName",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.sqlObject"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(0.75) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(1.5) } }),
      },
    },
    {
      name: "acknowledgedObjects",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.issuesAck/Total"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(0.75) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(1.5) } }),
        customBodyRender: (count: any) => {
          return <>{`${count.ackCount} / ${count.totalCount}`}</>;
        },
      },
    },

  ];

  const showPlusMinusExpansion: boolean = graphData.some(
    (item: {
      plusMinusExpansion: {
        mapReturnedArrayLength: number
      }
    }) =>
      item.plusMinusExpansion.mapReturnedArrayLength > 0
  );

  const showRightArrowSidePanel: boolean = graphData.some(
    (item) => item.rightArrowSidePanel.mapReturnedArrayLength > 0
  );

  const createCustomHeaderLabelRender =
    (labelKey: string, tooltipKey?: string): () => React.ReactNode => {
      return () => (
        <Box display="inline-flex" alignItems="center">
          {t(labelKey)}
          {tooltipKey && (
            <YBTooltip
              title={t(tooltipKey)}
              placement="top"
              interactive
            />
          )}
        </Box>
      );
    };

  const renderExpandedRow = (dataIndex: number, isExpanded: boolean) => {
    const isExpandable: boolean =
      graphData[dataIndex]?.plusMinusExpansion?.mapReturnedArrayLength > 0;
    if (!isExpandable) {
      return null;
    }
    // Access raw data from graphData using dataIndex
    const rawObjectType: string | undefined = graphData[dataIndex]?.objectType;
    const objectType: string = rawObjectType?.toLowerCase?.() ?? "";
    const rawTableData: FileIssue[] = expandableGraphData.get(objectType) ?? [];
    const tableData: FileIssue[] = rawTableData;

    // Get or initialize pagination state for this object type
    const hardcodedValues: { currentPage: number; rowsPerPage: number } = {
      currentPage: 0,
      rowsPerPage: 10,
    };
    const currentPagination: {
      currentPage: number;
      rowsPerPage: number;
    } = paginationState[objectType] || hardcodedValues;
    const { currentPage, rowsPerPage } = currentPagination;

    // Calculate visible rows for current page
    const startIndex: number = currentPage * rowsPerPage;
    const endIndex: number = Math.min(startIndex + rowsPerPage, tableData.length);
    const visibleRowsCount: number = endIndex - startIndex;

    // Helper functions to update pagination
    const updateCurrentPage = (newPage: number) => {
      setPaginationState((prev: {
        [key: string]: { currentPage: number; rowsPerPage: number }
      }) => ({
        ...prev,
        [objectType]: { ...currentPagination, currentPage: newPage }
      }));
    };

    const updateRowsPerPage = (newRowsPerPage: number) => {
      setPaginationState((prev: {
        [key: string]: { currentPage: number; rowsPerPage: number }
      }) => ({
        ...prev,
        [objectType]: { currentPage: 0, rowsPerPage: newRowsPerPage }
      }));
    };

    return (
      <TableRow className={classes.collapsibleRow}>
        <TableCell colSpan={columns.length} className={classes.collapsibleCell}>
          <Collapse in={isExpanded} timeout="auto" collapsedSize={0}>
            <Box className={classes.innerTableParent}>
              <Typography variant="h6" className={classes.expandedRowTitle}>
                <ArrowRightIcon className={classes.arrowIcon} />
                {t("clusterDetail.voyager.planAndAssess.refactoring.invalidObjects")}
              </Typography>
              <Box className={classes.innerTable}>
                <Box className={classes.expandedContent}>
                  {/* Left side dividers */}
                  <Box className={classes.dividerContainer}>
                    {Array.from({ length: visibleRowsCount }).map((_, index) => (
                      <Box key={index} className={classes.dividerRow}>
                        <InnerTableDivider />
                      </Box>
                    ))}
                  </Box>

                  {/* Table */}
                  <Box className={classes.tableContainer}>
                    <YBTable
                      key={`innerTable`}
                      data={tableData}
                      columns={innerColumns}
                      options={{
                        pagination: true,
                        responsive: 'standard',
                        onChangePage: (currentPage: number) => updateCurrentPage(currentPage),
                        onChangeRowsPerPage: (numberOfRows: number) =>
                          updateRowsPerPage(numberOfRows)
                      }}
                      withBorder={true}
                    />
                  </Box>
                </Box>
              </Box>
            </Box>
          </Collapse>
        </TableCell>
      </TableRow>
    );
  };

  const columns = [
    {
      name: "plusMinusExpansion",
      options: {
        sort: false,
        display: showPlusMinusExpansion,
        setCellHeaderProps: () => ({ className: classes.cellHeader }),
        setCellProps: () => ({
          width: "40px",
          height: "32px",
          padding: "0",
          textAlign: "center",
          verticalAlign: "middle"
        }),
        customBodyRender: (plusMinusExpansion: { mapReturnedArrayLength: number; index: number }) =>
          plusMinusExpansion.mapReturnedArrayLength > 0 && (
            <Box
              className={classes.expansionButton}
              onClick={() => {
                setExpandedSuggestions((prev) => ({
                  ...prev,
                  [plusMinusExpansion.index]: !prev[plusMinusExpansion.index],
                }));
              }}
            >
              {expandedSuggestions[plusMinusExpansion.index] ? <MinusIcon /> : <PlusIcon />}
            </Box>
          ),
        customHeadLabelRender: () => null
      },
    },
    {
      name: "objectType",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.objectType"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: "8px 0" } }),
        setCellProps: () => ({ style: { padding: "8px 0" } }),
        customHeadLabelRender: createCustomHeaderLabelRender(
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.objectType"
        ),
        customBodyRender: (value: string) => (
          <Typography className={classes.objectTypeText}>
            {capitalizeFirstLetter(value)}
          </Typography>
        ),
      },
    },
    {
      name: "automaticDDLImport",
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 0" } }),
        setCellProps: () => ({ style: { padding: "8px 0" } }),
        customBodyRender: (count: number) => (
          <YBBadge text={
            <Typography
              className={classes.badgeTextStyle}
              variant="body2">
              {count}
            </Typography>
          } variant={BadgeVariant.Success} />
        ),
        customHeadLabelRender: createCustomHeaderLabelRender(
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.automaticDDLImport",
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
          "automaticDDLImportTooltip"
        ),
      },
    },
    {
      name: "invalidObjCount",
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 0" } }),
        setCellProps: () => ({ style: { padding: "8px 0" } }),
        customBodyRender: (count: number, tableMeta: any) => {
          const rowIndex = tableMeta?.rowIndex;
          const objectType = tableMeta?.rowData?.[1]?.toLowerCase();
          return (
            <Box display="flex" flexDirection="row" alignItems="center">
              {
                count > 0 && (
                  <>
                    <YBBadge
                      text={
                        <Typography
                          className={classes.badgeTextStyle}
                          variant="body2">
                          {count}
                        </Typography>
                      }
                      variant={BadgeVariant.Warning}
                    />
                    {isAssessmentPage === false && <Box
                      component="span"
                      className={classes.viewDetailsLink}
                      onClick={() => {
                        setExpandedSuggestions((prev) => ({
                          ...prev,
                          [rowIndex]: !prev[rowIndex],
                        }));
                      }}
                    >
                      {t("clusterDetail.voyager.planAndAssess.sourceEnv.viewDetails")}
                    </Box>}
                  </>
                )
              }
              {count === 0 && isAssessmentPage === false && (
                <Box
                  component="span"
                  className={classes.seeDetailsHyphen}
                >
                  -
                </Box>
              )}
              {isAssessmentPage && (
                count > 0 ? (
                  <Box
                    component="span"
                    className={classes.seeDetailsLink}
                    onClick={() => {
                      setSelectedObjectType?.(formatSnakeCase(objectType));
                      scrollToIssueTable?.();
                      objectTypeSelectRef?.current?.focus();
                    }}
                  >
                    {t("clusterDetail.voyager.planAndAssess.refactoring.seeDetails")}
                  </Box>
                ) : (
                  <Box
                    component="span"
                    className={classes.seeDetailsHyphen}
                  >
                    -
                  </Box>
                )
              )}
            </Box>
          );
        },
        customHeadLabelRender: createCustomHeaderLabelRender(
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.invalidObjectCount",
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
          "invalidObjectCountTooltip"
        ),
      },
    },
    {
      name: "manualRefactoring",
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 25px" } }),
        setCellProps: () => ({ style: { padding: "8px 30px" } }),
        customBodyRender: (count: number, tableMeta: any) => {
          const rowIndex = tableMeta?.rowIndex;
          return (
            <Box display="flex" flexDirection="row" alignItems="center">
              {count > 0 ? (
                <>
                  <YBBadge text={
                    <Typography
                      className={classes.badgeTextStyle}
                      variant="body2">
                      {count}
                    </Typography>
                  } variant={BadgeVariant.Warning} />
                  {isAssessmentPage === false && (
                    <Box
                      component="span"
                      className={classes.viewDetailsLink}
                      onClick={() => {
                        setExpandedSuggestions((prev) => ({
                          ...prev,
                          [rowIndex]: !prev[rowIndex],
                        }));
                      }}
                    >
                      {t("clusterDetail.voyager.planAndAssess.sourceEnv.viewDetails")}
                    </Box>
                  )}
                </>
              ) : (
                <Box
                  component="span"
                  className={classes.seeDetailsHyphen}
                >
                  -
                </Box>
              )}
            </Box>
          );
        },
        display: isAssessmentPage === false, // Hiding this column in assessment page.
        customHeadLabelRender: createCustomHeaderLabelRender(
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.manualRefactoring",
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
          "manualRefactoringTooltip"
        ),
      },
    },
    {
      name: "rightArrowSidePanel",
      options: {
        sort: false,
        display: showRightArrowSidePanel,
        setCellHeaderProps: () => ({ style: { width: "50px", padding: "8px 8px" } }),
        setCellProps: () => ({ style: { width: "50px", padding: "8px 8px" } }),
        customBodyRender: (rightArrowSidePanel: {
          sqlObjectType: string | undefined | null;
          mapReturnedArrayLength: number;
        }) =>
          rightArrowSidePanel.mapReturnedArrayLength > 0 && (
            <Box
              className={classes.rightArrowButton}
              onClick={() => {
                const objectType: string = rightArrowSidePanel.sqlObjectType?.toLowerCase() ?? "";
                let dataForSidePanel: SqlObjectsDetails = {
                  objectType: "",
                  totalCount: 0,
                  invalidCount: 0,
                  objectNames: "",
                  issues: [],
                };
                const sidePanelSuggestionIssues: AID[] = [];
                let totalCount: number = 0;
                sqlObjectsList?.forEach((sqlObjects) => {
                  sqlObjects.issues?.forEach((currentIssue) => {
                    if (currentIssue && sqlObjects &&
                      sqlObjects?.objectType?.toLowerCase() === objectType) {
                      sidePanelSuggestionIssues.push(currentIssue);
                      totalCount = sqlObjects?.totalCount ?? 0;
                    }
                  });
                });
                dataForSidePanel.objectType = objectType.toUpperCase();
                dataForSidePanel.issues = sidePanelSuggestionIssues;
                dataForSidePanel.invalidCount = sidePanelSuggestionIssues.length;
                dataForSidePanel.totalCount = totalCount;
                setSelectedDataType(dataForSidePanel);
                const sideHeader = t(
                  "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
                  "compatibilityIssues"
                );
                setSidePanelHeader(sideHeader);
              }}
            >
              <CaretRightIconBlue />
            </Box>
          ),
        customHeadLabelRender: () => null, // No heading needed here
      },
    },
  ];

  return (
    <>
      <Box position="relative" className={classes.tableWrapper}>
        <YBTable
          data={graphData}
          columns={columns}
          withBorder={false}
          options={{
            pagination: false,
            customRowRender: (data: any, dataIndex: number) => {
              return (
                <Fragment key={`row-${dataIndex}`}>
                  <TableRow>
                    {data.map((cellData: any, cellIndex: number) => {
                      const cellProps = columns[cellIndex]?.options?.setCellProps?.() || {};
                      const style = 'style' in cellProps ? cellProps.style : cellProps;
                      return (
                        columns[cellIndex].options.display !== false && (
                          <TableCell key={`cell-${dataIndex}-${cellIndex}`} style={style}>
                            {typeof cellData === "function"
                              ? (cellData as any)(dataIndex)
                              : cellData}
                          </TableCell>
                        )
                      );
                    })}
                  </TableRow>
                  {renderExpandedRow(dataIndex, expandedSuggestions[dataIndex] === true)}
                </Fragment>
              );
            },
          }}
        />
        {/*
          Display Collapse/Expand button only if there are expandable objects in the graph data
        */}
        {(graphData?.some(
          ({ plusMinusExpansion }: { plusMinusExpansion: { mapReturnedArrayLength: number } }) =>
            plusMinusExpansion.mapReturnedArrayLength > 0
        ) ??
          false) && (
            <Box position="absolute" right={0} top={0}>
              <YBButton
                variant="ghost"
                startIcon={getExpandCollapseStatus ? <ExpandIcon /> : <CollapseIcon />}
                onClick={() => {
                  graphData?.forEach(({ plusMinusExpansion }) => {
                    if (plusMinusExpansion.mapReturnedArrayLength > 0) {
                      setExpandedSuggestions((prev) => ({
                        ...prev,
                        [plusMinusExpansion.index]: getExpandCollapseStatus,
                      }));
                    }
                  });
                }}
              >
                {getExpandCollapseStatus
                  ? t("clusterDetail.voyager.planAndAssess.refactoring.expandAll")
                  : t("clusterDetail.voyager.planAndAssess.refactoring.collapseAll")}
              </YBButton>
            </Box>
          )}
      </Box>

      <MigrationRefactoringSidePanel
        data={selectedDataType}
        onClose={() => {
          setSelectedDataType(undefined);
          setSidePanelHeader("");
        }}
        acknowledgedObjects={acknowledgedObjects}
        header={sidePanelHeader}
        toggleAcknowledgment={toggleAcknowledgment}
      />
    </>
  );
};
