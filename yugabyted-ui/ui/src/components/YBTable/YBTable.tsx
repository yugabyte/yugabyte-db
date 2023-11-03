import React, { ReactElement, useState } from 'react';
import MUIDataTable, {
  MUIDataTableColumn,
  MUIDataTableCustomHeadRenderer,
  MUIDataTableOptions,
  MUISortOptions,
  MUIDataTableCheckboxProps,
  MUIDataTableMeta
} from 'mui-datatables';
import {
    createStyles,
    makeStyles,
    Paper,
    TableCell,
    TableContainer,
    TableSortLabel,
    Theme
} from '@material-ui/core';
import { ArrowDropUp, ArrowDropDown } from '@material-ui/icons';
import { YBCheckbox } from '@app/components/YBCheckbox/YBCheckbox';
import { YBRadio } from '@app/components/YBRadio/YBRadio';
import clsx from 'clsx';
import type { ClassNameMap } from '@material-ui/styles';

interface YBTableProps {
  title?: string;
  selectableRows?: 'single' | 'multiple' | 'none';
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  data: (Record<any, any> | number[] | string[])[];
  columns: DataTableColumn[];
  options?: MUIDataTableOptions;
  withBorder?: boolean;
  touchBorder?: boolean;
  cellBorder?: boolean;
  noCellBottomBorder?: boolean;
  alternateRowShading?: boolean;
}

interface ColumnMeta extends MUIDataTableCustomHeadRenderer {
  hideHeader?: boolean;
}

interface DataTableColumn extends MUIDataTableColumn {
    subColumns?: MUIDataTableColumn[];
    customColumnSort?: (order: MUISortOptions['direction']) =>
        (obj1: { data: any; }, obj2: { data: any; }) => number;
    customColumnBodyRender?: (value: any, index: number) => React.ReactNode;
}

const useStyles = makeStyles((theme: Theme) => {
  return createStyles({
    root: {
      '& .MuiTableSortLabel-root': {
        maxHeight: theme.spacing(3),
      },
      '& .expandRow': {
        '& .MuiTableCell-root': {
          borderBottom: 0,
          
        },
        '&:hover': {
          backgroundColor: theme.palette.common.white
        },
        '+ tr': {
          '&:hover': {
            backgroundColor: theme.palette.common.white
          }
        }
      }
    },
    noBorderContainer: {
      '& .MuiPaper-root': {
        border: 0
      },
      '& .MuiTableContainer-root': {
        padding: 0,
        margin: 0
      }
    },
    touchBorderContainer: {
        '& .MuiTableContainer-root': {
          padding: 0,
          margin: 0
        }
    },
    cellBorders: {
        '& .MuiTableCell-root + .MuiTableCell-root': {
            borderLeft: `1px solid ${theme.palette.grey[300]}`,
            padding: 0
        },
    },
    noCellBottomBorder: {
      '& .MuiTableCell-body': {
        borderBottom: 0,
      },
    },
    subColumnHead: {
        borderTop: `1px solid ${theme.palette.grey[300]}`,
        whiteSpace: 'nowrap',
        padding: '8px 8px 8px 12px',
        flexGrow: 1
    },
    subColumnBody: {
        padding: '0px 8px 0px 12px',
        flexGrow: 1
    },
    alternateRowShading: {
        '& .MuiTableBody-root': {
            '& .MuiTableRow-root:nth-child(odd)': {
                backgroundColor: theme.palette.grey[100]
            }
        }
    },
    dropdown: {
      display: 'inline-grid'
    },
    expandClass: {
      width: theme.spacing(3)
    },
    headerWithSubcolumns: {
        whiteSpace: 'nowrap',
        padding: '8px',
        textAlign: 'center'
    },
    subcolumnHeads: {
        display: 'flex'
    },
    bodyWithSubcolumns: {
        display: 'flex'
    }
  });
});

// DropdownIndicator component is for up-down arrows for sorting column
const DropdownIndicator = (props: { active: boolean; direction: string }) => {
  const classes = useStyles();

  return (
    <div className={classes.dropdown}>
      <ArrowDropUp
        viewBox="1 -9 24 24"
        color={props.active && props.direction === 'asc' ? 'primary' : 'disabled'}
      />
      <ArrowDropDown
        viewBox="1 9 24 24"
        color={props.active && props.direction === 'desc' ? 'primary' : 'disabled'}
      />
    </div>
  );
};

const getSubColumnHeads = (
    header: ColumnMeta,
    subColumns: MUIDataTableColumn[],
    sortOrder: MUISortOptions,
    updateDirection: (arg0: number) => void,
    subColumnSortIndex: number,
    setSubColumnSortIndex: React.Dispatch<React.SetStateAction<number>>,
    classes: ClassNameMap,
) => {
    return (
        <div className={classes.subcolumnHeads}>
            {subColumns.map((subColumn, index) => {
            if (subColumn.options?.display) {
                let restProps = {};
                if (subColumns[index].options?.setCellHeaderProps) {
                    restProps = subColumns[index].options!.setCellHeaderProps!(header);
                }
                return (
                    <div
                        className={classes.subColumnHead}
                        key={subColumn.name}
                        onClick={() => {
                            setSubColumnSortIndex(index);
                            updateDirection(header.index);
                        }}
                        {...restProps}
                    >
                        {subColumn.label}
                        <TableSortLabel
                        active={header.name === sortOrder.name && subColumnSortIndex === index}
                        IconComponent={() => (
                            <DropdownIndicator
                                active={
                                    header.name === sortOrder.name && subColumnSortIndex === index
                                }
                                direction={sortOrder.direction}
                            />
                        )}
                        direction={header.name === sortOrder.name ? sortOrder.direction : 'asc'}
                        />
                    </div>
                )
            }
            return
            })}
        </div>
    )
}

// cHeadRender component is for up-down arrows for sorting column
// If don't want to show header then add label = 'Empty' in columns list
const cHeadRender = (
  header: ColumnMeta,
  updateDirection: (arg0: number) => void,
  sortOrder: MUISortOptions,
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  headerProps?: (meta: ColumnMeta) => Record<any, any>,
  sort?: boolean
) => {
  let restProps = {};
  if (headerProps) {
    restProps = { ...headerProps(header) };
  }
  return (
    <TableCell key={header.name} onClick={() => updateDirection(header.index)} {...restProps}>
      {!header?.hideHeader && header.label}
      {!header?.hideHeader && sort && (
        <TableSortLabel
          active={header.name === sortOrder.name}
          IconComponent={() => (
            <DropdownIndicator
                active={header.name === sortOrder.name}
                direction={sortOrder.direction}
            />
          )}
          direction={header.name === sortOrder.name ? sortOrder.direction : 'asc'}
        />
      )}
    </TableCell>
  );
};

const cHeadRenderWithSubColumns = (
  header: ColumnMeta,
  updateDirection: (arg0: number) => void,
  sortOrder: MUISortOptions,
  subColumns: MUIDataTableColumn[],
  subColumnSortIndex: number,
  setSubColumnSortIndex: React.Dispatch<React.SetStateAction<number>>,
  classes: ClassNameMap,
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  headerProps?: (meta: ColumnMeta) => Record<any, any>,
  sort?: boolean
) => {
  let restProps = {};
  if (headerProps) {
    restProps = { ...headerProps(header) };
  }
  return (
    <TableCell key={header.name} {...restProps}>
      <div className={classes.headerWithSubcolumns}>
        {!header?.hideHeader && header.label}
      </div>
      {!header?.hideHeader && sort && (
            getSubColumnHeads(
                header,
                subColumns,
                sortOrder,
                updateDirection,
                subColumnSortIndex,
                setSubColumnSortIndex,
                classes
            )
      )}
    </TableCell>
  );
};

const cBodyRenderWithSubColumns = (
    value: any,
    tableMeta: MUIDataTableMeta,
    subColumns: MUIDataTableColumn[],
    classes: ClassNameMap,
    customColumnBodyRender?: (value: any, index: number) => React.ReactNode
) => {
    const visibleColumns = subColumns.map(subColumn => subColumn.options?.display);
    if (Array.isArray(value)){
        return (
            <div className={classes.bodyWithSubcolumns}>
            {value.map((data, index) => {
                if (visibleColumns[index]) {
                    let restProps = {}
                    if (subColumns[index].options?.setCellProps) {
                        restProps = subColumns[index].options!
                            .setCellProps!(data, tableMeta.rowIndex, tableMeta.columnIndex);
                    }
                    return (
                        <div className={classes.subColumnBody} {...restProps}>
                            {customColumnBodyRender ? customColumnBodyRender(data, index) : data}
                        </div>
                    )
                }
                return
            })}
            </div>
        )
    }
    return value;
}

// CustomCheckbox When multi-select rows then show checkbox
const CustomCheckbox = (props: MUIDataTableCheckboxProps) => {
  const newProps = { ...props };
  newProps.color = 'primary';
  return <YBCheckbox label="" {...newProps} />;
};

// CustomRadio When single-select rows then show radio
const CustomRadio = (props: { [x: string]: string }) => {
  const newProps = Object.assign({}, props);
  newProps.color = 'primary';
  if (props['data-description'] === 'row-select') {
    return <YBRadio {...newProps} />;
  }
  return '';
};

// In case 2 row template then this will remove expand button
const ExpandButton = () => {
  const classes = useStyles();
  return <div className={classes.expandClass} />;
};

export const YBTable = ({
  title = '',
  selectableRows = 'none',
  withBorder = true,
  touchBorder = false,
  noCellBottomBorder = false,
  cellBorder = false,
  alternateRowShading = false,
  data,
  columns,
  options
}: YBTableProps): ReactElement => {
  const classes = useStyles();
  const overriddenOptions: MUIDataTableOptions = {
    selectableRows,
    elevation: 0,
    rowHover: false,
    filter: false,
    search: false,
    download: false,
    print: false,
    viewColumns: false,
    responsive: 'standard',
    setTableProps: () => ({ size: 'small' }),
    ...options // put it last to allow overriding default options
  };
  const [subColumnSortIndex, setSubColumnSortIndex] = useState(0);
  const cols = columns.map((col) => {
    if (col?.options) {
      if (col.subColumns) {
        // For rendering columns with subcolumns
        col.options.customHeadRender = (
            columnMeta: ColumnMeta,
            updateDirection: (arg0: number) => void,
            sortOrder: MUISortOptions
        ) => cHeadRenderWithSubColumns(
            columnMeta,
            updateDirection,
            sortOrder,
            col.subColumns!,
            subColumnSortIndex,
            setSubColumnSortIndex,
            classes,
            col.options?.setCellHeaderProps,
            col.options?.sort ?? true
        );
        col.options.customBodyRender = (
            value: any,
            tableMeta: MUIDataTableMeta
        ) => cBodyRenderWithSubColumns(
            value,
            tableMeta,
            col.subColumns!,
            classes,
            col.customColumnBodyRender
        );
        if (col.customColumnSort) {
            col.options.sortCompare = (order) => (obj1, obj2) => {
                return col.customColumnSort!(order)(
                    obj1.data[subColumnSortIndex],
                    obj2.data[subColumnSortIndex]
                );
            }
        } else {
            // This will be the default sortCompare function for columns that have subcolumns.
            col.options.sortCompare = (order) => (obj1, obj2) => {
                let val1 = 0;
                let val2 = 0;
                if (Array.isArray(obj1.data)) {
                    val1 = obj1.data[subColumnSortIndex];
                    val2 = obj2.data[subColumnSortIndex];
                } else {
                    val1 = obj1.data;
                    val2 = obj2.data;
                }
                let compareResult = val2 < val1 ? 1 : -1;
                if (val2 == val1) {
                    compareResult = 0;
                }
                return compareResult * (order === 'asc' ? 1 : -1);
            }
        }
      } else {
        col.options.customHeadRender = (
            columnMeta: ColumnMeta,
            updateDirection: (arg0: number) => void,
            sortOrder: MUISortOptions
        ) => cHeadRender(columnMeta,
            updateDirection,
            sortOrder,
            col.options?.setCellHeaderProps,
            col.options?.sort ?? true
        );
        if (col.customColumnSort) {
            col.options.sortCompare = col.customColumnSort;
        }
      }
    }
    return col;
  });

  var tableContainerDivClasses = [classes.root];
  if (!withBorder) {
    tableContainerDivClasses.push(classes.noBorderContainer);
  }
  if (touchBorder) {
    tableContainerDivClasses.push(classes.touchBorderContainer);
  }
  if (cellBorder) {
    tableContainerDivClasses.push(classes.cellBorders);
  }
  if (alternateRowShading) {
    tableContainerDivClasses.push(classes.alternateRowShading);
  }
  if (noCellBottomBorder) {
    tableContainerDivClasses.push(classes.noCellBottomBorder);
  }
  const tableContainerDiv = clsx(tableContainerDivClasses);
  return (
    <div className={tableContainerDiv}>
      <TableContainer component={Paper}>
        <MUIDataTable
          title={title}
          data={data}
          columns={cols}
          options={overriddenOptions}
          components={{
            Checkbox: selectableRows === 'single' ? CustomRadio : CustomCheckbox,
            ExpandButton
          }}
        />
      </TableContainer>
    </div>
  );
};
/* eslint-enable @typescript-eslint/ban-types */
