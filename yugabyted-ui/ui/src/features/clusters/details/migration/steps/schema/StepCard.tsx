import React, { FC } from "react";
import { Box, LinearProgress, makeStyles, Paper, Typography, useTheme } from "@material-ui/core";
import TodoIcon from "@app/assets/todo.svg";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import { YBTooltip } from "@app/components";

const useStyles = makeStyles((theme) => ({
  paper: {
    borderColor: theme.palette.grey[200],
  },
  dotWrapper: {
    height: "32px",
    width: "32px",
    flexShrink: 0,
    borderRadius: "100%",
    border: "1px dashed",
    borderColor: theme.palette.grey[300],
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
  },
  dot: {
    height: "16px",
    width: "16px",
    borderRadius: "50%",
    backgroundColor: theme.palette.grey[200],
  },
  badge: {
    height: "32px",
    width: "32px",
    borderRadius: "50%",
  },
  progressbar: {
    height: "8px",
    borderRadius: "5px",
  },
  bar: {
    borderRadius: "5px",
  },
  barBg: {
    backgroundColor: theme.palette.grey[200],
  },
}));

interface StepCardProps {
  title: string;
  showTooltip?: boolean;
  isDone?: boolean;
  isLoading?: boolean;
  children?: (isDone: boolean) => React.ReactNode;
}

export const StepCard: FC<StepCardProps> = ({
  title,
  showTooltip = false,
  isDone = false,
  isLoading = false,
  children,
}) => {
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation();

  const totalObjects = 25;
  const completedObjects = 12;
  const progress = Math.round((completedObjects / totalObjects) * 100);

  const content = children?.(isDone);

  return (
    <Paper className={classes.paper}>
      <Box p={2}>
        <Box display="flex" alignItems="center" gridGap={theme.spacing(3)}>
          {!isDone && !isLoading && (
            <Box className={classes.dotWrapper}>
              <Box className={classes.dot} />
            </Box>
          )}
          {(isDone || isLoading) && (
            <YBBadge
              className={classes.badge}
              text=""
              variant={isDone ? BadgeVariant.Success : BadgeVariant.InProgress}
            />
          )}
          <Box flex={1} display="flex" alignItems="center" gridGap={6}>
            <Typography variant="body2">{title}</Typography>
            {showTooltip && (
              <Box>
                <YBTooltip title={t("clusterDetail.voyager.migrateSchema.completeStepsTooltip")} />
              </Box>
            )}
          </Box>
          {!isDone && !isLoading && !showTooltip && (
            <YBBadge
              variant={BadgeVariant.InProgress}
              text={t("clusterDetail.voyager.todo")}
              iconComponent={TodoIcon}
            />
          )}
        </Box>

        {isLoading && (
          <Box ml={7} mt={2}>
            <LinearProgress
              classes={{
                root: classes.progressbar,
                colorPrimary: classes.barBg,
                bar: classes.bar,
              }}
              variant="determinate"
              value={progress}
            />
            <Box ml="auto" mt={1} width="fit-content">
              <Typography variant="body2">
                {completedObjects}/{totalObjects} objects completed
              </Typography>
            </Box>
          </Box>
        )}

        {!isLoading && !showTooltip && content && (
          <Box ml={7} mt={2}>
            {content}
          </Box>
        )}
      </Box>
    </Paper>
  );
};
