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

export interface VoyagerInstanceProps {
  machineIP: string;
  os: string;
  totalDisk: string;
  usedDisk: string;
  exportDir: string;
  exportedSchemaLocation: string;
}

interface MigrationListVoyagerSidePanel extends VoyagerInstanceProps {
  open: boolean;
  onClose: () => void;
}

export const MigrationListVoyagerSidePanel: FC<MigrationListVoyagerSidePanel> = ({
  open,
  onClose,
  machineIP,
  os,
  totalDisk,
  usedDisk,
  exportDir,
  exportedSchemaLocation,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <YBModal
      open={open}
      title={t("clusterDetail.voyager.voyagerSidePanel.voyagerInstance")}
      onClose={onClose}
      enableBackdropDismiss
      titleSeparator
      cancelLabel={t("common.close")}
      isSidePanel
    >
      <Box my={2}>
        <Grid container spacing={4}>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.machineIP")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {machineIP}
            </Typography>
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.os")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {os}
            </Typography>
          </Grid>
          <Grid item xs={12} className={classes.dividerGridItem}>
            <Divider orientation="horizontal" />
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.totalDiskSpace")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {totalDisk}
            </Typography>
          </Grid>
          <Grid item xs={6}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.usedDiskSpace")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {usedDisk}
            </Typography>
          </Grid>
          <Grid item xs={12} className={classes.dividerGridItem}>
            <Divider orientation="horizontal" />
          </Grid>
          <Grid item xs={12}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.exportDirectory")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {exportDir}
            </Typography>
          </Grid>
          <Grid item xs={12}>
            <Typography variant="subtitle2" className={classes.label}>
              {t("clusterDetail.voyager.voyagerSidePanel.exportedSchemaLocation")}
            </Typography>
            <Typography variant="body2" className={classes.value}>
              {exportedSchemaLocation}
            </Typography>
          </Grid>
        </Grid>
      </Box>
    </YBModal>
  );
};
