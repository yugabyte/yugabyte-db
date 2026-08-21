#!/usr/bin/env bash
printf '{"permission":"deny","user_message":"%s%s"}\n' \
  'Blocked: raw git push is disabled. ' \
  'Use .agents/scripts/git-push.sh to push to your fork instead.'
