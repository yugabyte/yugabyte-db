import React, { FC } from "react";
import { Box, Paper, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { Link } from "react-router-dom";

import { YBButton } from "@app/components";

import WelcomeCloudImage from "@app/assets/welcome-cloud-image.svg";

import { makeStyles } from "@material-ui/core";

const MIGRATIONS_DOCS = "https://docs.yugabyte.com/preview/yugabyte-voyager/migrate-steps/";

export const useStyles = makeStyles((theme) => ({
  root: {
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    maxWidth: 1200,
    width: "100%",
    minHeight: 325,
    padding: theme.spacing(0, 4),
    margin: theme.spacing(0, "auto"),
    borderRadius: theme.shape.borderRadius,
    border: "none",
  },
}));

export const MigrationsGetStarted: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Paper className={classes.root}>
      <Box display="flex" maxWidth={1000}>
        <WelcomeCloudImage />
        <Box pt={1} pl={5} flex={1}>
          <Box mb={1.5}>
            <Typography variant="h3">{t("clusterDetail.voyager.noMigrations")}</Typography>
          </Box>
          <Box mb={3}>
            <Typography variant="body2" style={{ lineHeight: 1.5 }}>
              {t("clusterDetail.voyager.getStarted")}
            </Typography>
          </Box>
          <YBButton
            variant="gradient"
            size="large"
            component={Link}
            to={{ pathname: MIGRATIONS_DOCS }}
            target="_blank"
          >
            {t("clusterDetail.voyager.learnMore")}
          </YBButton>
        </Box>
      </Box>
    </Paper>
  );
};
