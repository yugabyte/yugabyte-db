import React, { FC } from "react";
import { Box, makeStyles } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { YBTable } from "@app/components";
import type { Migration } from "./MigrationOverview";

const useStyles = makeStyles((theme) => ({
  arrowComponent: {
    textAlign: "end",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
  },
  hardComp: {
    color: theme.palette.error.main,
  },
  mediumComp: {
    color: theme.palette.warning[700],
  },
  easyComp: {
    color: theme.palette.success.main,
  },
}));

const ComplexityComponent = (classes: ReturnType<typeof useStyles>) => (complexity: string) => {
  const className =
    complexity === "Hard"
      ? classes.hardComp
      : complexity === "Medium"
      ? classes.mediumComp
      : complexity === "Easy"
      ? classes.easyComp
      : undefined;

  return <Box className={className}>{complexity}</Box>;
};

const StatusComponent = () => (status: string) => {
  return (
    <Box>
      <YBBadge variant={BadgeVariant.InProgress} text={status} />
    </Box>
  );
};

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
  );
};

interface MigrationListProps {
  migrationData: Migration[];
  onSelectMigration: (migration: Migration) => void;
}

export const MigrationList: FC<MigrationListProps> = ({
  migrationData,
  onSelectMigration,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const migrationColumns = [
    {
      name: "name",
      label: t("clusterDetail.voyager.name"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "complexity",
      label: t("clusterDetail.voyager.complexity"),
      options: {
        customBodyRender: ComplexityComponent(classes),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "status",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: StatusComponent(),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "",
      label: "",
      options: {
        sort: false,
        customBodyRender: ArrowComponent(classes),
      },
    },
  ];

  return (
    <Box display="flex" flexDirection="column" gridGap={10}>
      <YBTable
        data={migrationData}
        columns={migrationColumns}
        options={{
          pagination: false,
          rowHover: true,
          onRowClick: (_, { dataIndex }) => onSelectMigration(migrationData[dataIndex]),
        }}
        touchBorder={false}
      />
    </Box>
  );
};
