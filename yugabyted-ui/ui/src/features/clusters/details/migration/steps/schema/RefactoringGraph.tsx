import React, { FC, useMemo, Fragment } from "react";
import type { RefactoringCount } from "@app/api/src";
import { useTranslation } from "react-i18next";
import { YBTable, YBButton } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { Box, TableCell, TableRow, makeStyles } from "@material-ui/core";
import type { UnsupportedSqlWithDetails, ErrorsAndSuggestionsDetails } from "@app/api/src";
import MinusIcon from "@app/assets/minus_icon.svg";
import PlusIcon from "@app/assets/plus_icon.svg";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import ExpandIcon from "@app/assets/expand.svg";
import CollapseIcon from "@app/assets/collapse.svg";
import { useQueryParams } from "@app/helpers";
import { MigrationRefactoringSidePanel } from "../assessment/AssessmentRefactoringSidePanel";
interface RefactoringGraphProps {
  sqlObjects: RefactoringCount[] | undefined;
  sqlObjectsList?: UnsupportedSqlWithDetails[] | undefined;
}

type ESD = ErrorsAndSuggestionsDetails;
interface FileIssue {
  objectType: ESD["issueType"];
  filePath: ESD["filePath"];
  objectName: ESD["objectName"];
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
  },
}));

const ACKNOWLEDGED_OBJECTS_LOCAL_STORAGE_KEY: string = "acknowledgedObjects";

export const RefactoringGraph: FC<RefactoringGraphProps> = ({ sqlObjects, sqlObjectsList }) => {
  const { t } = useTranslation();
  const classes = useStyles();
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
    React.useState<UnsupportedSqlWithDetails | undefined>(undefined);

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
      if (sqlObject.suggestions_errors) {
        sqlObject.suggestions_errors.forEach((error) => {
          const objectType = error?.objectType?.toLowerCase() ?? "";

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
            const filteredErrors = sqlObj.suggestions_errors?.filter(
              (error) => error.filePath === filePath && error.objectName === objectName
            );
            return count + (filteredErrors?.length ?? 0);
          }, 0);
          if (existingIssueIndex === -1) {
            existingIssues.push({
              objectType: error.objectType,
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

    const objectTypeNameMap = new Map<string, ESD["objectName"][]>();
    sqlObjectsList?.forEach((sqlObject: UnsupportedSqlWithDetails) => {
      const currentSuggestionErrors: ESD[] = sqlObject?.suggestions_errors || [];

      currentSuggestionErrors.forEach((item) => {
        const objectType: string = item.objectType?.toLowerCase() ?? "";
        const objectName: string = item.objectName?.toLowerCase() ?? "";

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
          objectType: sql_object_type?.trim().toLowerCase(),
          automaticDDLImport: automatic ?? 0,
          manualRefactoring: manual ?? 0,
          invalidObjCount: invalid ?? 0,
          rightArrowSidePanel: {
            mapReturnedArrayLength: doesMapReturnArray ? mapReturnedArray?.length : 0,
            sqlObjectType: sql_object_type?.trim().toLowerCase(),
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
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
    {
      name: "filePath",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.fileDirectory"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px", "word-break": "break-word" } }),
      },
    },
    {
      name: "objectName",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.sqlObject"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
    {
      name: "acknowledgedObjects",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.issuesAck/Total"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
        customBodyRender: (count: any) => {
          return <>{`${count.ackCount} / ${count.totalCount}`}</>;
        },
      },
    },
  ];
  const showPlusMinusExpansion = graphData.some(
    (item) => item.plusMinusExpansion.mapReturnedArrayLength > 0
  );
  const showRightArrowSidePanel = graphData.some(
    (item) => item.rightArrowSidePanel.mapReturnedArrayLength > 0
  );

  const columns = [
    {
      name: "plusMinusExpansion",
      label: " ",
      options: {
        sort: false,
        display: showPlusMinusExpansion,
        setCellHeaderProps: () => ({ style: { width: "20px", padding: "8px 8px" } }),
        setCellProps: () => ({ style: { width: "20px", padding: "8px 8px" } }),
        customBodyRender: (plusMinusExpansion: { mapReturnedArrayLength: number; index: number }) =>
          plusMinusExpansion.mapReturnedArrayLength > 0 && (
            <Box
              p={1}
              style={{ cursor: "pointer" }}
              onClick={() => {
                setExpandedSuggestions((prev) => ({
                  ...prev,
                  [plusMinusExpansion.index]: !prev[plusMinusExpansion.index],
                }));
              }}
            >
              {expandedSuggestions[plusMinusExpansion.index] ? <MinusIcon /> : <PlusIcon />}
            </Box>
          )
      },
    },
    {
      name: "objectType",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.objectType"),
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: "8px 30px" } }),
        setCellProps: () => ({ style: { padding: "8px 30px", textTransform: "capitalize" } })
      },
    },
    {
      name: "automaticDDLImport",
      label: t(
        "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.automaticDDLImport"
      ),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 30px" } }),
        setCellProps: () => ({ style: { padding: "8px 30px" } }),
        customBodyRender: (count: number) => (
          <YBBadge text={count} variant={BadgeVariant.Success} />
        )
      },
    },
    {
      name: "invalidObjCount",
      label: t(
        "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.invalidObjectCount"
      ),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 25px" } }),
        setCellProps: () => ({ style: { padding: "8px 30px" } }),
        customBodyRender: (count: number) => (
          <YBBadge text={count} variant={BadgeVariant.Warning} />
        )
      },
    },
    {
      name: "manualRefactoring",
      label: t(
        "clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.manualRefactoring"
      ),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 25px" } }),
        setCellProps: () => ({ style: { padding: "8px 30px" } }),
        customBodyRender: (count: number) => (
          <YBBadge text={count} variant={BadgeVariant.Warning} />
        )
      },
    },
    {
      name: "rightArrowSidePanel",
      label: " ",
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
              style={{ cursor: "pointer" }}
              onClick={() => {
                const objectType: string = rightArrowSidePanel.sqlObjectType?.toLowerCase() ?? "";
                let dataForSidePanel: UnsupportedSqlWithDetails = {
                  unsupported_type: "",
                  suggestions_errors: [],
                  count: 0,
                };
                const sidePanelSuggestionErrors: ESD[] = [];
                sqlObjectsList?.forEach((sqlObjects) => {
                  sqlObjects.suggestions_errors?.forEach((currentSuggestionError) => {
                    if (
                      currentSuggestionError &&
                      currentSuggestionError?.objectType?.toLowerCase() === objectType
                    )
                      sidePanelSuggestionErrors.push(currentSuggestionError);
                  });
                });
                dataForSidePanel.unsupported_type = objectType.toUpperCase();
                dataForSidePanel.suggestions_errors = sidePanelSuggestionErrors;
                dataForSidePanel.count = sidePanelSuggestionErrors.length;
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
          )
      },
    },
  ];

  return (
    <>
      <Box position="relative">
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
                        columns[cellIndex].options.display !== false && (
                          <TableCell key={`cell-${dataIndex}-${cellIndex}`} style={cellProps.style}>
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
                            data={expandableGraphData.get(data[1]) ?? []}
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
       {/* Display Collapse/Expand button only if there are expandable objects in the graph data */}
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
