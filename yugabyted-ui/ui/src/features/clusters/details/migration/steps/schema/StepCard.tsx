import React, { FC } from "react";
import { Box, makeStyles, Paper, Typography, useTheme } from "@material-ui/core";
import TodoIcon from "@app/assets/todo.svg";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import { YBAccordion, YBTooltip } from "@app/components";
import type { Trans as TransType } from "react-i18next";
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
    borderRadius: "100%",
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

type StepCardStatus = "TODO" | "IN_PROGRESS" | "DONE" | "WARNING";

interface StepCardProps {
  title: string | React.ReactElement<typeof TransType>;
  showTooltip?: boolean;
  showTodo?: boolean;
  showInProgress?: boolean;
  hideContent?: boolean;
  isTodo?: boolean;
  isDone?: boolean;
  isLoading?: boolean;
  accordion?: boolean;
  defaultExpanded?: boolean;
  contentSeparator?: boolean;
  renderChips?: () => React.ReactNode;
  children?: (state: StepCardStatus) => React.ReactNode;
}

export const StepCard: FC<StepCardProps> = ({
  title,
  showTooltip = false,
  showTodo = false,
  showInProgress = false,
  hideContent = false,
  isTodo = false,
  isDone = false,
  isLoading = false,
  accordion = false,
  contentSeparator = false,
  defaultExpanded = isLoading,
  renderChips,
  children,
}) => {
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation();

  const getStepCardStatus = (isDone: boolean, isLoading: boolean, isTodo: boolean) => {
    if (isLoading) {
      return "IN_PROGRESS";
    }
    if (isTodo) {
      return "WARNING";
    }
    return isDone ? "DONE" : "TODO";
  };

  const content = children?.(getStepCardStatus(isDone, isLoading, isTodo));

  return (
    <Accordionify
      accordion={accordion}
      renderChips={renderChips}
      defaultExpanded={defaultExpanded}
      contentSeparator={contentSeparator}
      titleComponent={
        <Box display="flex" alignItems="center" gridGap={theme.spacing(3)}>
          {!isDone && !isLoading && !isTodo && (
            <Box className={classes.dotWrapper}>
              <Box className={classes.dot} />
            </Box>
          )}
          {((isDone && !isTodo) || isLoading) && (
            <YBBadge
              className={classes.badge}
              text=""
              variant={isDone ? BadgeVariant.Success : BadgeVariant.InProgress}
            />
          )}
          {isTodo &&  (
            <YBBadge
              className={classes.badge}
              text=""
              iconComponent={TodoIcon}
              variant={BadgeVariant.InProgress}
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
          {showTodo && (
            <YBBadge
              variant={BadgeVariant.InProgress}
              text={t("clusterDetail.voyager.todo")}
              iconComponent={TodoIcon}
            />
          )}
          {showInProgress && (
            <YBBadge
              variant={BadgeVariant.InProgress}
              text={t("clusterDetail.voyager.inProgress")}
            />
          )}
        </Box>
      }
    >
      {content && !hideContent && (
        <Box ml={7} mt={2}>
          {content}
        </Box>
      )}
    </Accordionify>
  );
};

interface AccordionifyProps {
  accordion?: boolean;
  titleComponent: React.ReactNode;
  renderChips?: () => React.ReactNode;
  children: React.ReactNode;
  defaultExpanded?: boolean;
  contentSeparator?: boolean;
}

export const Accordionify: FC<AccordionifyProps> = ({
  titleComponent,
  accordion = false,
  renderChips,
  children,
  defaultExpanded,
  contentSeparator
}) => {
  const classes = useStyles();

  if (accordion) {
    return (
      <YBAccordion
        titleContent={titleComponent}
        renderChips={renderChips}
        defaultExpanded={defaultExpanded}
        contentSeparator={contentSeparator}
      >
        <Box width="100%" mt={-3}>
          {children}
        </Box>
      </YBAccordion>
    );
  }

  return (
    <Paper className={classes.paper}>
      <Box p={2}>
        {titleComponent}
        {children}
      </Box>
    </Paper>
  );
};
