import React, { FC, useEffect, useState } from "react";
import { Box, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBCheckboxField, YBModal } from "@app/components";
import { useForm } from "react-hook-form";

const useStyles = makeStyles((theme) => ({
  modalCheckboxesComponent: {
    columnCount: 1,
  },
  modalCheckboxSection: {
    breakInside: "avoid",
    marginBottom: "6px",
  },
  modalCheckboxChild: {
    marginLeft: "32px",
    display: "flex",
    flexDirection: "column",
  },
  nodeName: {
    color: theme.palette.grey[900],
  },
  nodeHost: {
    color: theme.palette.grey[700],
  },
  checkbox: {
    padding: "6px 6px 6px 6px",
  },
}));

const MIGRATION_COLUMNS_LS_KEY = "migration-columns";

interface MigrationListColumnsProps {
  open: boolean;
  onClose: () => void;
  onUpdateColumns: (columns: Record<string, boolean>) => void;
}

export const MigrationListColumns: FC<MigrationListColumnsProps> = ({
  open,
  onClose,
  onUpdateColumns,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const CHECKBOXES = {
    general: {
      label: t("clusterDetail.voyager.listColumns.general"),
      columns: [
        {
          name: "migration_uuid",
          label: t("clusterDetail.voyager.listColumns.migrationUUID"),
          disabled: true,
        },
        {
          name: "migration_type",
          label: t("clusterDetail.voyager.listColumns.migrationType"),
        },
      ],
    },
    sourceDB: {
      label: t("clusterDetail.voyager.listColumns.sourceDatabase"),
      columns: [
        {
          name: "hostname",
          label: t("clusterDetail.voyager.listColumns.hostname"),
        },
        {
          name: "host_ip",
          label: t("clusterDetail.voyager.listColumns.hostIpAddrPort"),
        },
        {
          name: "engineVersion",
          label: t("clusterDetail.voyager.listColumns.engineVersion"),
        },
        {
          name: "database",
          label: t("clusterDetail.voyager.listColumns.database"),
        },
        {
          name: "schema",
          label: t("clusterDetail.voyager.listColumns.schema"),
        },
      ],
    },
    voyagerInstance: {
      label: t("clusterDetail.voyager.listColumns.voyagerInstance"),
      columns: [
        {
          name: "machineIP",
          label: t("clusterDetail.voyager.listColumns.machineIP"),
        },
        {
          name: "os",
          label: t("clusterDetail.voyager.listColumns.os"),
        },
        {
          name: "availableDiskSpace",
          label: t("clusterDetail.voyager.listColumns.availableDiskSpace"),
        },
        {
          name: "exportDir",
          label: t("clusterDetail.voyager.listColumns.exportDirectory"),
        },
      ],
    },
    targetDB: {
      label: t("clusterDetail.voyager.listColumns.targetDatabase"),
      columns: [
        {
          name: "clusterUUID",
          label: t("clusterDetail.voyager.listColumns.clusterUUID"),
        },
        {
          name: "ybaYbm",
          label: t("clusterDetail.voyager.listColumns.ybaOrYbm"),
        },
      ],
    },
    complexity: {
      label: t("clusterDetail.voyager.listColumns.complexity"),
      columns: [],
    },
    progress: {
      label: t("clusterDetail.voyager.listColumns.progress"),
      disabled: true,
      columns: [],
    },
  };

  const defaultValues: Record<string, boolean> = {
    // categories, for the edit columns modal
    general: true,
    sourceDB: true,
    voyagerInstance: true,
    targetDB: true,
    complexity: true,
    progress: true,
    // columns, including parent and subcolumns
    migration_uuid: true,
    migration_type: true,
    hostname: false,
    host_ip: true,
    engineVersion: true,
    database: false,
    schema: false,
    machineIP: true,
    os: false,
    availableDiskSpace: false,
    exportDir: false,
    clusterUUID: true,
    ybaYbm: true,
  };

  const CHECKBOX_PARENTS = {
    migration_uuid: "general",
    migration_type: "general",
    hostname: "sourceDB",
    host_ip: "sourceDB",
    engineVersion: "sourceDB",
    database: "sourceDB",
    schema: "sourceDB",
    machineIP: "voyagerInstance",
    os: "voyagerInstance",
    availableDiskSpace: "voyagerInstance",
    exportDir: "voyagerInstance",
    clusterUUID: "targetDB",
    ybaYbm: "targetDB",
  };

  const [columns, setColumns] = useState({
    ...defaultValues,
    ...(JSON.parse(localStorage.getItem(MIGRATION_COLUMNS_LS_KEY)!) || {}),
  });

  const { control, handleSubmit, reset, setValue, getValues } = useForm({
    mode: "onChange",
    defaultValues: columns,
  });

  const applyColumnChanges = handleSubmit((formData) => {
    setColumns(formData);
    localStorage.setItem(MIGRATION_COLUMNS_LS_KEY, JSON.stringify(formData));
    onClose();
  });

  useEffect(() => {
    onUpdateColumns(columns);
  }, [columns]);

  const handleClose = () => {
    reset(columns);
    onClose();
  };

  return (
    <YBModal
      open={open}
      title={t("clusterDetail.nodes.editColumns")}
      onClose={handleClose}
      onSubmit={applyColumnChanges}
      enableBackdropDismiss
      titleSeparator
      submitLabel={t("common.apply")}
      cancelLabel={t("common.cancel")}
      isSidePanel
    >
      <Box mt={1} mb={1}>
        <Typography variant="body2">Display the following columns</Typography>
      </Box>

      <div className={classes.modalCheckboxesComponent}>
        {Object.entries(CHECKBOXES).map(([key, value]) => {
          return (
            <div className={classes.modalCheckboxSection}>
              <YBCheckboxField
                name={key}
                label={
                  <Typography variant="body1" className={classes.nodeName}>
                    {value.label}
                  </Typography>
                }
                disabled={"disabled" in value && value.disabled}
                control={control}
                className={classes.checkbox}
                onChange={(e) => {
                  // Selecting a category checkbox selects all columns under it
                  setValue(key, e.target.checked);
                  for (const column of value.columns) {
                    if ("disabled" in column && column.disabled && !e.target.checked) {
                      continue;
                    }
                    setValue(column.name, e.target.checked);
                  }
                }}
              />
              <div className={classes.modalCheckboxChild}>
                {value.columns.map((column: any) => {
                  return (
                    <YBCheckboxField
                      name={column.name}
                      label={column.label}
                      control={control}
                      disabled={"disabled" in column && column.disabled}
                      className={classes.checkbox}
                      onChange={(e) => {
                        // Selecting at least one column will cause parent checkbox to be
                        // selected.
                        // If all boxes of a category are deselected, the parent is also
                        // deselected.
                        setValue(column.name, e.target.checked);
                        let parentName =
                          CHECKBOX_PARENTS[column.name as keyof typeof CHECKBOX_PARENTS];
                        let orAllChildren = (
                          CHECKBOXES[parentName as keyof typeof CHECKBOXES].columns as any[]
                        ).reduce(
                          (accumulator: any, current: any) =>
                            accumulator ||
                            (getValues(current.name) && "disabled" in current && !current.disabled),
                          false
                        );
                        setValue(parentName, e.target.checked || orAllChildren);
                      }}
                    />
                  );
                })}
              </div>
            </div>
          );
        })}
      </div>
    </YBModal>
  );
};
