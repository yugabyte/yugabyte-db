import React, { FC } from "react";
import { Box, Link, Paper, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import MigrateIcon from "@app/assets/migration48.svg";
import BookIcon from "@app/assets/book.svg";
import { makeStyles } from "@material-ui/core";
import { YBButton } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";

const MIGRATIONS_DOCS = "https://docs.yugabyte.com/preview/yugabyte-voyager/migrate-steps/";

export const useStyles = makeStyles((theme) => ({
  paper: {
    border: "1px dashed",
    borderColor: theme.palette.primary[300],
    backgroundColor: theme.palette.primary[100],
    textAlign: "center",
  },
  icon: {
    marginTop: theme.spacing(1),
    flexShrink: 0,
  },
  message: {
    color: theme.palette.grey[700],
  },
  refreshButton: {
    marginLeft: "auto",
    width: "fit-content",
    marginBottom: theme.spacing(2),
  }
}));

type MigrationsGetStartedProps = {
  onRefresh?: () => void;
};

export const MigrationsGetStarted: FC<MigrationsGetStartedProps> = ({ onRefresh }) => {
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation();

  return (
    <Box>
      <Box className={classes.refreshButton}>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefresh}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
      </Box>
      <Paper className={classes.paper}>
        <Box
          p={3}
          display="flex"
          flexDirection="column"
          alignItems="center"
          gridGap={theme.spacing(3)}
        >
          <MigrateIcon className={classes.icon} />
          <Box
            display="flex"
            flexDirection="column"
            alignItems="center"
            gridGap={theme.spacing(2.5)}
          >
            <Typography variant="h5">
              {t("clusterDetail.voyager.gettingStarted.noMigrations")}
            </Typography>
            <Typography variant="body2" className={classes.message}>
              {t("clusterDetail.voyager.gettingStarted.noMigrationsDesc")}
            </Typography>
            <Box display="flex" gridGap={theme.spacing(1.5)} alignItems="center">
              <BookIcon />
              <Link href={MIGRATIONS_DOCS} target="_blank">
                {t("clusterDetail.voyager.gettingStarted.learnMore")}
              </Link>
            </Box>
            <Box display="flex" gridGap={theme.spacing(0.5)} alignItems="center" mt={1.5}>
              <Typography variant="body1">Note!</Typography>
              <Typography variant="body2" className={classes.message}>
                {t("clusterDetail.voyager.gettingStarted.refreshNote")}
              </Typography>
            </Box>
          </Box>
        </Box>
      </Paper>
    </Box>
  );
};
