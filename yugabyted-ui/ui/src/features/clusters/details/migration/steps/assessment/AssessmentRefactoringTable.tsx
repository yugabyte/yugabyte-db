import React, { FC, useMemo } from "react";
import { Box } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTable } from "@app/components";
import type { UnsupportedSqlInfo } from "@app/api/src";

/* const ENABLE_MORE_DETAILS = false;

const useStyles = makeStyles((theme) => ({
  arrowComponent: {
    textAlign: "end",
    cursor: "pointer",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
  },
  tableCell: {
    padding: theme.spacing(1, 2),
    maxWidth: 120,
    wordBreak: "break-word",
  },
  rowTableCell: {
    borderBottom: "unset",
  },
  w10: {
    width: "10px",
  },
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
})); */

interface MigrationAssessmentRefactoringTableProps {
  data: UnsupportedSqlInfo[] | undefined;
  tableHeader: string;
}

/* const getRowCellComponent = (
  displayedRows: RefactoringDataItems,
  expanded: boolean[],
  classes: ReturnType<typeof useStyles>
) => {
  const { t } = useTranslation();

  const innerColumns = [
    {
      name: "objecttype",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.objectType"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
    {
      name: "fileDirectory",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.fileDirectory"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
    {
      name: "totalObjects",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.totalObjects"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
    {
      name: "acknowledgedObjects",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.acknowledgedObjects"),
      options: {
        customBodyRenderLite: (dataIndex: number) =>
          `${innerData[dataIndex].acknowledgedObjects} / ${innerData[dataIndex].totalObjects}`,
        setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
        setCellProps: () => ({ style: { padding: "6px 16px" } }),
      },
    },
  ];

  const innerData = [
    {
      objecttype: "View",
      fileDirectory: "/home/nikhil/tradex/schema/views/view.sql",
      totalObjects: 7,
      acknowledgedObjects: 2,
    },
    {
      objecttype: "Table",
      fileDirectory: "/home/nikhil/tradex/schema/tables/table.sql",
      totalObjects: 6,
      acknowledgedObjects: 0,
    },
  ];

  const rowCellComponent = (data: any, dataIndex: number) => {
    return (
      <Fragment key={`row-fragment-${data}`}>
        <TableRow>
          {data.map((val: any, index: number) => (
            <TableCell
              key={`row-${dataIndex}-body-cell-${index}`}
              className={clsx(
                classes.tableCell,
                expanded[dataIndex] && classes.rowTableCell,
                index === 0 && classes.w10
              )}
            >
              {typeof val === "function" ? val(dataIndex) : val}
            </TableCell>
          ))}
        </TableRow>
        {expanded[dataIndex] && (
          <TableRow>
            <TableCell colSpan={4} className={classes.innerTableParent}>
              <Box className={classes.innerTable}>
                <YBTable
                  data={innerData}
                  columns={innerColumns}
                  options={{
                    pagination: false,
                  }}
                />
              </Box>
            </TableCell>
          </TableRow>
        )}
      </Fragment>
    );
  };
  return rowCellComponent;
};

const ArrowComponent = (classes: ReturnType<typeof useStyles>, onClick: () => void) => () => {
  return (
    <Box className={classes.arrowComponent} onClick={onClick}>
      <ArrowRightIcon />
    </Box>
  );
}; */

export const MigrationAssessmentRefactoringTable: FC<MigrationAssessmentRefactoringTableProps> = ({
  tableHeader,
  data,
}) => {
  /* const classes = useStyles(); */
  const { t } = useTranslation();

  const tableData = useMemo(() => {
    if (!data) {
      return [];
    }

    return data.map(({ unsupported_type, count }) => ({
      type: unsupported_type,
      count: count ?? 0,
    }));
  }, [data]);

  /* const [selectedDataType, setSelectedDataType] = React.useState<RefactoringDataItems[number]>();
  const [expandedSuggestions, setExpandedSuggestions] = React.useState<boolean[]>([]); */

  const refactoringOverviewColumns = [
    /* ...(ENABLE_MORE_DETAILS
      ? [
          {
            name: "",
            label: "",
            options: {
              sort: false,
              customBodyRenderLite: (dataIndex: number) => (
                <Box
                  px={1}
                  style={{ cursor: "pointer" }}
                  onClick={() => {
                    const newExpandedSuggestions = [...expandedSuggestions];
                    newExpandedSuggestions[dataIndex] = !expandedSuggestions[dataIndex];
                    setExpandedSuggestions(newExpandedSuggestions);
                  }}
                >
                  {expandedSuggestions[dataIndex] ? <MinusIcon /> : <PlusIcon />}
                </Box>
              ),
            },
          },
        ]
      : []), */
    {
      name: "type",
      label: tableHeader,
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "count",
      label: t("clusterDetail.voyager.planAndAssess.refactoring.objectCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    /* ...(ENABLE_MORE_DETAILS
      ? [
          {
            name: "",
            label: "",
            options: {
              sort: false,
              customBodyRenderLite: () => ArrowComponent(classes, () => {})(),
              setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
            },
          },
        ]
      : []), */
  ];

  return (
    <Box>
      <Box position="relative">
        <YBTable
          data={tableData}
          columns={refactoringOverviewColumns}
          options={{
            /* customRowRender: ENABLE_MORE_DETAILS
                ? getRowCellComponent(featureOverview, expandedSuggestions, classes)
                : undefined, */
            pagination: tableData.length > 0,
          }}
          withBorder
        />

        {/* {ENABLE_MORE_DETAILS && (
            <Box display="flex" justifyContent="end" position="absolute" right={10} top={6}>
              <YBButton
                variant="ghost"
                startIcon={
                  expandedSuggestions.filter((s) => s).length < featureOverview.length ? (
                    <ExpandIcon />
                  ) : (
                    <CollpaseIcon />
                  )
                }
                onClick={() => {
                  setExpandedSuggestions(
                    new Array(featureOverview.length).fill(
                      expandedSuggestions.filter((s) => s).length < featureOverview.length
                    )
                  );
                }}
              >
                {expandedSuggestions.filter((s) => s).length < featureOverview.length
                  ? t("clusterDetail.voyager.planAndAssess.refactoring.expandAll")
                  : t("clusterDetail.voyager.planAndAssess.refactoring.collapseAll")}
              </YBButton>
            </Box>
          )} */}
      </Box>
      {/* <MigrationRefactoringSidePanel
        data={selectedDataType}
        onClose={() => setSelectedDataType(undefined)}
      /> */}
    </Box>
  );
};
