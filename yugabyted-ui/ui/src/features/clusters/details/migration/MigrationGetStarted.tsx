import React, { FC } from "react";
import { Box, Link, Paper, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import BookIcon from "@app/assets/book.svg";
import PlusIcon from '@app/assets/plus.svg';
import { makeStyles } from "@material-ui/core";
import { YBButton } from "@app/components";

const MIGRATIONS_DOCS = "https://docs.yugabyte.com/preview/yugabyte-voyager/migrate-steps/";

export const useStyles = makeStyles((theme) => ({
  paper: {
    border: "none",
    borderTop: `1px solid ${theme.palette.grey[300]}`,
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
  onNewMigration?: () => void;
};

export const MigrationsGetStarted: FC<MigrationsGetStartedProps> = ({
    onNewMigration,
}) => {
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation();

  return (
    <Box>
      <Paper className={classes.paper} square>
        <Box
          p={3}
          display="flex"
          flexDirection="column"
          alignItems="center"
          gridGap={theme.spacing(3)}
        >
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
            <YBButton
              variant="primary"
              onClick={onNewMigration}
              startIcon={<PlusIcon />}
            >
              {t('clusterDetail.voyager.gettingStarted.migrateDatabase')}
            </YBButton>

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
