import React, { FC } from "react";
import {
  Box,
  Grid,
  Typography,
  makeStyles,
  useTheme,
  List,
  ListItem
} from "@material-ui/core";
import { Link } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBAccordion } from "@app/components";
import { parseHTML } from "@app/helpers";
import { extractUrlsAndText } from "@app/helpers";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left"
  },
  dividerVertical: {
    marginLeft: theme.spacing(5),
    marginRight: theme.spacing(5)
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start"
  },
  recommendationCard: {
    display: "flex",
    flexDirection: "row",
    justifyContent: "space-between",
    width: "100%",
    paddingRight: theme.spacing(2),
    paddingLeft: theme.spacing(2),
    paddingTop: theme.spacing(2),
    paddingBottom: theme.spacing(2)
  },
  noteItem: {
    marginBottom: theme.spacing(1.5)
  },
  link: {
    color: theme.palette.primary.main,
    textDecoration: "none",
    "&:hover": {
      textDecoration: "underline",
      color: theme.palette.primary.dark
    }
  }
}));

interface RecommendedNotesProps {
  notes: string[] | undefined;
}

export const RecommendedNotes: FC<RecommendedNotesProps> = ({ notes }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  if (!notes || notes.length === 0) {
    return null;
  }

  return (
    <YBAccordion
      titleContent={
        <Box display="flex" alignItems="center" gridGap={theme.spacing(3)}>
          {t("clusterDetail.voyager.planAndAssess.recommendation.notes.heading")}
        </Box>
      }
      defaultExpanded
      contentSeparator
    >
      <Box className={classes.recommendationCard}>
        <Grid container spacing={4}>
          <Grid item xs={12}>
            <List
              style={{
                paddingLeft: theme.spacing(3),
                margin: 0,
                listStyleType: "disc",
                paddingInlineStart: "20px"
              }}
            >
              {notes.map((note, index) => {
                const parsedNodes = parseHTML(note);

                return (
                  <ListItem key={index} className={classes.noteItem}
                    style={{ display: "list-item" }}>
                    <Typography variant="body1" component="span">
                    {Array.from(parsedNodes).map((node, nodeIndex) => {
                      if (node.nodeType === Node.TEXT_NODE) {
                        const text: string = node.textContent || '';
                        const parts = extractUrlsAndText(text);

                        return (
                          <React.Fragment key={nodeIndex}>
                            {parts.map((part, i) => (
                              part.type === 'url' ? (
                                <Link
                                  key={i}
                                  href={part.content}
                                  target="_blank"
                                  rel="noopener noreferrer"
                                  className={classes.link}
                                >
                                  {part.content}
                                </Link>
                              ) : (
                                <React.Fragment key={i}>{part.content}</React.Fragment>
                              )
                            ))}
                          </React.Fragment>
                        );
                      }

                      if (node.nodeType === Node.ELEMENT_NODE
                          && node instanceof HTMLAnchorElement) {
                        const href: string = node.href;

                        return (
                          <Link
                            key={nodeIndex}
                            href={href}
                            target="_blank"
                            className={classes.link}
                          >
                            {node.textContent}
                          </Link>
                        );
                      }
                      return null;
                    })}
                    </Typography>
                  </ListItem>
                );
              })}
            </List>
          </Grid>
        </Grid>
      </Box>
    </YBAccordion>
  );
};
