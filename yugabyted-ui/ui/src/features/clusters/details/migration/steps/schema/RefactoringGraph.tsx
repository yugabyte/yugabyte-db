import React, { FC, useMemo, Fragment } from "react";
import type { RefactoringCount } from "@app/api/src";
import { useTranslation } from "react-i18next";
import { YBTable, YBButton, YBTooltip } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { Box, TableCell, TableRow, makeStyles, Typography, useTheme } from "@material-ui/core";
import type { SqlObjectsDetails, AnalysisIssueDetails } from "@app/api/src";
import MinusIcon from "@app/assets/minus_icon.svg";
import PlusIcon from "@app/assets/plus_icon.svg";
import ArrowRightIcon from "@app/assets/caretRightIconBlue.svg";
import ExpandIcon from "@app/assets/expand.svg";
import CollapseIcon from "@app/assets/collapse.svg";
import { capitalizeFirstLetter, formatSnakeCase, useQueryParams } from "@app/helpers";
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

const useStyles = makeStyles((theme) => ({
  innerTable: {
    borderRadius: theme.shape.borderRadius,
    backgroundColor: theme.palette.background.default,
    marginLeft: theme.spacing(5),
    "& .MuiPaper-root": {
      backgroundColor: "transparent !important",
    },
  },
  innerTableParent: {
    padding: theme.spacing(0.5, 0, 1, 0),
    position: "relative",
  },
  seeDetailsLink: {
    color: theme.palette.primary[600],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.subtitle1.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.body2.fontWeight,
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
    fontSize: theme.typography.subtitle1.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.body2.fontWeight,
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
    width: theme.spacing(4),
    padding: 0,
    textAlign: 'center',
    verticalAlign: 'middle',
  },
  badgeTextStyle: {
    minWidth: '16px',
    display: 'inline-block',
    textAlign: 'center',
    fontSize: `${theme.typography.subtitle1.fontSize}px !important`,
    fontFamily: theme.typography.fontFamily,
    fontStyle: 'normal',
    fontWeight: theme.typography.body2.fontWeight,
    lineHeight: '16px'
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
  const migrationUUID: string = queryParams.get("migration_uuid") ?? "";

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
  const [sidePanelTitle, setSidePanelTitle] = React.useState<string>("");

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
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(0.75, 0) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(0.75, 0) } }),
      },
    },
    {
      name: "filePath",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.fileDirectory"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(0.75, 0) } }),
        setCellProps: () => ({
          style: { padding: theme.spacing(0.75, 0), "word-break": "break-word" },
        }),
      },
    },
    {
      name: "objectName",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.sqlObject"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(0.75, 0) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(0.75, 0) } }),
      },
    },
    {
      name: "acknowledgedObjects",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.issuesAck/Total"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(0.75, 0) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(0.75, 0) } }),
        customBodyRender: (count: any) => {
          return <>{`${count.ackCount} / ${count.totalCount}`}</>;
        },
      },
    },
  ];

  const showPlusMinusExpansion: boolean = graphData.some(
    (item) => item.plusMinusExpansion.mapReturnedArrayLength > 0
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

  const columns = [
    {
      name: "plusMinusExpansion",
      options: {
        sort: false,
        display: showPlusMinusExpansion,
        setCellHeaderProps: () => ({ className: classes.cellHeader }),
        setCellProps: () => ({
          width: "32px",
          height: "32px",
          padding: "0",
          textAlign: "center",
          verticalAlign: "middle"
        }),
        customBodyRender: (plusMinusExpansion: { mapReturnedArrayLength: number; index: number }) =>
          plusMinusExpansion.mapReturnedArrayLength > 0 && (
            <Box
              style={{
                cursor: "pointer",
                height: theme.spacing(4),
                width: theme.spacing(4),
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                margin: 0,
                padding: 0
              }}
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
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(1, 0) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(1, 0) } }),
        customHeadLabelRender: createCustomHeaderLabelRender(
          "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.objectType"
        ),
        customBodyRender: (value: string) => (
          <Typography style={{
            fontSize: theme.typography.fontSize,
            fontWeight: theme.typography.body2.fontWeight,
            color: theme.palette.text.primary,
            textTransform: 'capitalize'
          }}>
            {capitalizeFirstLetter(value)}
          </Typography>
        ),
      },
    },
    {
      name: "automaticDDLImport",
      options: {
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(1, 0) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(1, 0) } }),
        customBodyRender: (count: number) => (
          <YBBadge text={
            <Typography
              className={classes.badgeTextStyle}
              variant="body2">
                {count}
              </Typography>
          } variant={BadgeVariant.Success}/>
        ),
        customHeadLabelRender: createCustomHeaderLabelRender(
        "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.automaticDDLImport",
        "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.automaticDDLImportTooltip"
        ),
      },
    },
    {
      name: "invalidObjCount",
      options: {
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(1, 0) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(1, 0) } }),
        customBodyRender: (count: number, tableMeta: any) => {
          const objectType = tableMeta?.rowData?.[1]?.toLowerCase();

          if (count > 0) {
            return (
              <Box display="flex" flexDirection="row" alignItems="center">
                <YBBadge
                  text={
                    <Typography className={classes.badgeTextStyle} variant="body2">
                      {count}
                    </Typography>
                  }
                  variant={BadgeVariant.Warning}
                />
                {isAssessmentPage && (
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
                )}
              </Box>
            );
          }

          return (
            <Box component="span" className={classes.seeDetailsHyphen}>
              -
            </Box>
          );
        },
        customHeadLabelRender: createCustomHeaderLabelRender(
        "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.invalidObjectCount",
        "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.invalidObjectCountTooltip"
        ),
      },
    },
    {
      name: "manualRefactoring",
      options: {
        setCellHeaderProps: () => ({ style: { padding: theme.spacing(1, 3.125) } }),
        setCellProps: () => ({ style: { padding: theme.spacing(1, 3.75) } }),
        customBodyRender: (count: number) => (
          <YBBadge text={
            <Typography
              className={classes.badgeTextStyle}
              variant="body2">
                {count}
            </Typography>
          } variant={BadgeVariant.Warning}/>
        ),
        display: isAssessmentPage === false, // Hiding this column in assessment page.
        customHeadLabelRender: createCustomHeaderLabelRender(
         "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.manualRefactoring",
         "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.manualRefactoringTooltip"
        ),
      },
    },
    {
      name: "rightArrowSidePanel",
      options: {
        sort: false,
        display: showRightArrowSidePanel,
        setCellHeaderProps: () => ({
          style: { width: theme.spacing(6.25), padding: theme.spacing(1) },
        }),
        setCellProps: () => ({ style: { width: theme.spacing(6.25), padding: theme.spacing(1) } }),
        customBodyRender: (rightArrowSidePanel: {
          sqlObjectType: string | undefined | null;
          mapReturnedArrayLength: number;
        }) =>
          rightArrowSidePanel.mapReturnedArrayLength > 0 && (
            <Box
              style={{
                cursor: "pointer",
                height: theme.spacing(4),
                width: theme.spacing(6.25),
                display: "flex",
                alignItems: "center",
                justifyContent: "center"
              }}
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
              "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.compatibilityIssues"
                );
                setSidePanelHeader(sideHeader);
                const sideTitle = t(
                  "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issues"
                );
                setSidePanelTitle(sideTitle);
              }}
            >
              <ArrowRightIcon />
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
            customRowRender: (data, dataIndex) => {
              return (
                <Fragment key={`row-${dataIndex}`}>
                  <TableRow>
                    {data.map((cellData, cellIndex) => {
                      const cellProps = columns[cellIndex]?.options?.setCellProps?.() || {};
                      return (
                        columns[cellIndex].options.display !== false && cellData !== undefined && (
                          <TableCell key={`cell-${dataIndex}-${cellIndex}`}
                            style={(cellProps as any).style || cellProps}>
                            {typeof cellData === "function"
                              ? (cellData as any)(dataIndex)
                              : cellData}
                          </TableCell>
                        )
                      );
                    })}
                  </TableRow>
                  {expandedSuggestions[dataIndex] === true && (
                    <TableRow>
                      <TableCell colSpan={columns.length} className={classes.innerTableParent}>
                        <Box className={classes.innerTable}>
                          <YBTable
                            key={`innerTable`}
                            data={
                              expandableGraphData.get(
                                graphData[dataIndex]?.objectType?.toLowerCase() ?? ''
                              ) ?? []
                            }
                            columns={innerColumns}
                            options={{
                              pagination: true,
                            }}
                            withBorder={true}
                          />
                        </Box>
                      </TableCell>
                    </TableRow>
                  )}
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
          setSidePanelTitle("");
        }}
        acknowledgedObjects={acknowledgedObjects}
        header={sidePanelHeader}
        title={sidePanelTitle}
        toggleAcknowledgment={toggleAcknowledgment}
      />
    </>
  );
};
