import React, { FC } from "react";
import { Box, Breadcrumbs, Link, makeStyles, MenuItem, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBDropdown } from "@app/components";
import { MigrationList } from "./MigrationList";
import TriangleDownIcon from "@app/assets/caret-down.svg";
import { MigrationData } from "./MigrationData";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "start",
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(1.5),
    marginBottom: theme.spacing(1.5),
  },
  heading: {
    marginBottom: theme.spacing(5),
  },
  link: {
    "&:link, &:focus, &:active, &:visited, &:hover": {
      textDecoration: "none",
      color: theme.palette.text.primary,
    },
  },
  dropdown: {
    cursor: "pointer",
    marginRight: theme.spacing(1),
    display: "flex",
    flexDirection: "column"
  },
  dropdownContent: {
    color: "black",
  },
  dropdownHeader: {
    fontWeight: 500,
    color: theme.palette.grey[500],
    marginLeft: theme.spacing(2),
    marginRight: theme.spacing(2),
    fontSize: "11.5px",
    textTransform: "uppercase",
  },
}));

const migrationDataList = [
  {
    name: "Migration UUID1",
    complexity: "Easy",
    step: 0,
    starttime: "11/07/2022, 09:55",
  },
  {
    name: "Migration UUID2",
    complexity: "Medium",
    step: 1,
    starttime: "11/07/2022, 09:53",
  },
  {
    name: "Migration UUID3",
    complexity: "Hard",
    step: 3,
    starttime: "11/01/2022, 09:52",
  },
];

export type Migration = (typeof migrationDataList)[number];

interface MigrationOverviewProps {}

export const MigrationOverview: FC<MigrationOverviewProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const migrationSteps = [
    t("clusterDetail.voyager.analyzeSchema"),
    t("clusterDetail.voyager.exportData"),
    t("clusterDetail.voyager.importSchema"),
    t("clusterDetail.voyager.importData"),
    t("clusterDetail.voyager.verify"),
  ];

  const migrationData = migrationDataList.map((data) => {
    return {
      ...data,
      status: migrationSteps[data.step],
    };
  });

  const [selectedMigration, setSelectedMigration] = React.useState<Migration>();

  return (
    <Box display="flex" flexDirection="column" gridGap={10}>
      <Box>
        {selectedMigration && (
          <Breadcrumbs aria-label="breadcrumb">
            <Link
              className={classes.link}
              onClick={() => {
                setSelectedMigration(undefined);
              }}
            >
              <Typography variant="body2" color="primary">
                {t("clusterDetail.voyager.migrations")}
              </Typography>
            </Link>
            {selectedMigration && (
              <YBDropdown
                origin={
                  <Box display="flex" alignItems="center" className={classes.dropdownContent}>
                    {selectedMigration.name}
                    <TriangleDownIcon />
                  </Box>
                }
                position={"bottom"}
                growDirection={"right"}
                className={classes.dropdown}
              >
                <Box className={classes.dropdownHeader}>
                  {t("clusterDetail.voyager.migrations")}
                </Box>
                <Box display="flex" flexDirection="column" minWidth="150px">
                {migrationData.map((migration) => (
                  <MenuItem
                    key={migration.name}
                    selected={migration.name === selectedMigration.name}
                    onClick={() => setSelectedMigration(migration)}
                    
                  >
                    {migration.name}
                  </MenuItem>
                ))}
                </Box>
              </YBDropdown>
            )}
          </Breadcrumbs>
        )}
      </Box>

      {!selectedMigration ? (
        <MigrationList migrationData={migrationData} onSelectMigration={setSelectedMigration} />
      ) : (
        <MigrationData
          steps={migrationSteps}
          migration={selectedMigration}
        />
      )}
    </Box>
  );
};
