import React, { FC, Fragment, useMemo } from "react";
import { Box, TableCell, TableRow, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton, YBTable } from "@app/components";
import type { UnsupportedSqlWithDetails } from "@app/api/src";
import clsx from "clsx";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import MinusIcon from "@app/assets/minus_icon.svg";
import PlusIcon from "@app/assets/plus_icon.svg";
import ExpandIcon from "@app/assets/expand.svg";
import CollpaseIcon from "@app/assets/collapse.svg";
import { MigrationRefactoringSidePanel } from "./AssessmentRefactoringSidePanel";
import { getMappedData } from "./refactoringUtils";

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
}));

interface MigrationAssessmentRefactoringTableProps {
  data: UnsupportedSqlWithDetails[] | undefined;
  tableHeader: string;
  title: string;
  enableMoreDetails?: boolean;
}

const getRowCellComponent = (
  displayedRows: UnsupportedSqlWithDetails[] | undefined,
  expanded: boolean[],
  classes: ReturnType<typeof useStyles>
) => {
  const { t } = useTranslation();

  const rowCellComponent = (data: UnsupportedSqlWithDetails[], dataIndex: number) => {
    const innerColumns = [
      {
        name: "objectType",
        label: t("clusterDetail.voyager.planAndAssess.refactoring.objectType"),
        options: {
          setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
          setCellProps: () => ({ style: { padding: "6px 16px" } }),
        },
      },
      {
        name: "filePath",
        label: t("clusterDetail.voyager.planAndAssess.refactoring.fileDirectory"),
        options: {
          setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
          setCellProps: () => ({ style: { padding: "6px 16px", "word-break": "break-word" } }),
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
          setCellHeaderProps: () => ({ style: { padding: "6px 16px" } }),
          setCellProps: () => ({ style: { padding: "6px 16px" } }),
        },
      },
    ];

    const mappedData = getMappedData(displayedRows?.[dataIndex]?.suggestions_errors)

    const innerData = mappedData?.map((row) => ({
      objectType: row.groupKey,
      // Assumption here is that filePath is the same for all objects of the same objectType
      filePath: displayedRows?.[dataIndex]?.suggestions_errors?.[0]?.filePath ?? "",
      totalObjects: row.sqlStatements.length,
      // TODO: Replace 0 with the actual count of acknowledged objects
      acknowledgedObjects: 0,
    })) ?? [];

    return (
      <Fragment key={`row-fragment-${data}`}>
        <TableRow>
          {data.map((val, index) => (
            <TableCell
              key={`row-${dataIndex}-body-cell-${index}`}
              className={clsx(
                classes.tableCell,
                expanded[dataIndex] && classes.rowTableCell,
                index === 0 && classes.w10
              )}
            >
              {typeof val === "function" ? (val as any)(dataIndex) : val}
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
};

export const MigrationAssessmentRefactoringTable: FC<MigrationAssessmentRefactoringTableProps> = ({
  tableHeader,
  title,
  data,
  enableMoreDetails = false,
}) => {
  const classes = useStyles();
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

  const [selectedDataType, setSelectedDataType] = React.useState<UnsupportedSqlWithDetails>();
  const [expandedSuggestions, setExpandedSuggestions] = React.useState<boolean[]>([]);

  const refactoringOverviewColumns = [
    ...(enableMoreDetails
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
      : []),
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
    ...(enableMoreDetails
      ? [
          {
            name: "",
            label: "",
            options: {
              sort: false,
              customBodyRenderLite: (dataIndex: number) => ArrowComponent(classes, () => {
                setSelectedDataType(data?.[dataIndex]);
              })(),
              setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
            },
          },
        ]
      : []),
  ];

  return (
    <Box>
      <Box position="relative">
        <YBTable
          data={tableData}
          columns={refactoringOverviewColumns}
          options={{
            customRowRender: enableMoreDetails
                ? getRowCellComponent(data, expandedSuggestions, classes)
                : undefined,
            pagination: tableData.length > 0,
          }}
          withBorder
        />

        {enableMoreDetails && data?.length ? (
            <Box display="flex" justifyContent="end" position="absolute" right={10} top={6}>
              <YBButton
                variant="ghost"
                startIcon={
                  expandedSuggestions.filter((s) => s).length < data.length ? (
                    <ExpandIcon />
                  ) : (
                    <CollpaseIcon />
                  )
                }
                onClick={() => {
                  setExpandedSuggestions(
                    new Array(data.length).fill(
                      expandedSuggestions.filter((s) => s).length < data.length
                    )
                  );
                }}
              >
                {expandedSuggestions.filter((s) => s).length < data.length
                  ? t("clusterDetail.voyager.planAndAssess.refactoring.expandAll")
                  : t("clusterDetail.voyager.planAndAssess.refactoring.collapseAll")}
              </YBButton>
            </Box>
          ) : null}
      </Box>

      <MigrationRefactoringSidePanel
        data={selectedDataType}
        onClose={() => setSelectedDataType(undefined)}
        header={tableHeader}
        title={title}
      />
    </Box>
  );
};
