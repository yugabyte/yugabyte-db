import React, { FC } from "react";
import { Box, Divider, Grid, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBModal } from "@app/components";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left",
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  dividerGridItem: {
    padding: `${theme.spacing(1)}px ${theme.spacing(2)}px !important`,
  },
}));

export interface SourceDBProps {
  hostname: string;
  ip: string;
  port: string;
  engine: string;
  version: string;
  auth: string;
  database: string;
  schema: string;
};

interface MigrationListSourceDBSidePanel extends SourceDBProps {
  open: boolean;
  onClose: () => void;
}

export const MigrationListSourceDBSidePanel: FC<MigrationListSourceDBSidePanel> = ({
  open,
  onClose,
  hostname,
  ip,
  port,
  engine,
  version,
  auth,
  database,
  schema,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <YBModal
      open={open}
      title={t("clusterDetail.voyager.sourceDBSidePanel.sourceDatabase")}
      onClose={onClose}
      enableBackdropDismiss
      titleSeparator
      cancelLabel={t("common.close")}
      isSidePanel
    >
      <Box my={2}>
        <Grid container spacing={4}>
          <Grid item xs={12}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.sourceDBSidePanel.hostname")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {hostname}
            </Typography>
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.sourceDBSidePanel.ipAddress")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {ip}
            </Typography>
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.sourceDBSidePanel.tcpPort")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {port}
            </Typography>
          </Grid>
          <Grid item xs={12} className={classes.dividerGridItem}>
            <Divider orientation="horizontal" />
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.sourceDBSidePanel.engine")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {engine}
            </Typography>
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.sourceDBSidePanel.version")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {version}
            </Typography>
          </Grid>
          <Grid item xs={12} className={classes.dividerGridItem}>
            <Divider orientation="horizontal" />
          </Grid>
          <Grid item xs={12}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.sourceDBSidePanel.authType")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {auth}
            </Typography>
          </Grid>
          <Grid item xs={12} className={classes.dividerGridItem}>
            <Divider orientation="horizontal" />
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.sourceDBSidePanel.database")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {database}
            </Typography>
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.sourceDBSidePanel.schema")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {schema}
            </Typography>
          </Grid>
        </Grid>
      </Box>
    </YBModal>
  );
};
