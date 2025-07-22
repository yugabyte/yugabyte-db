#!/bin/bash

# Get current kube context
context=$(kubectl config current-context)

# Defaults for YBA
export STORAGE_CLASS="yb-standard"
export STORAGE_SIZE="100Gi"
export YBA_ADDITIONAL_VALUES_FILE="/dev/null"

# Derive cloud provider from context
if [[ "$context" == *"teleport"*"aws"* ]]; then
  export STORAGE_CLASS="gp2"
  export STORAGE_SIZE="100Gi"

  # -----------------------------
  # AWS-specific: Set role ARN
  # -----------------------------
  # Extract region-role from context name
  region_role=$(echo "$context" | sed -E 's/.*-aws-([a-z0-9-]+)$/\1/')

  # AWS account ID in devcloud
  export AWS_ACCOUNT_ID="745846189716"

  # Get namespace from kube config, fallback to "default"
  export AWS_NAMESPACE=$(kubectl config view --minify --output 'jsonpath={..namespace}')
  [[ -z "$AWS_NAMESPACE" ]] && AWS_NAMESPACE="default"

  # Construct role ARN
  export AWS_ROLE_ARN="arn:aws:iam::${AWS_ACCOUNT_ID}:role/${region_role}-${AWS_NAMESPACE}"
  export ENABLE_AWS_IAM_ROLE="true"

  export SERVICE_ACCOUNT_NAME=$(kubectl config view --minify --output 'jsonpath={..namespace}')
  [[ -z "$SERVICE_ACCOUNT_NAME" ]] && export SERVICE_ACCOUNT_NAME="default"

elif [[ "$context" == *"teleport"*"gcp"* ]]; then
  export STORAGE_CLASS="regional-standard"
  export STORAGE_SIZE="200Gi"

elif [[ "$context" == *"teleport"*"azure"* ]]; then
  export STORAGE_CLASS="managed-csi"
  export STORAGE_SIZE="200Gi"
fi

if [[ "$context" == *"teleport"* ]] && \
   ([[ "$context" == *"aws"* ]] || \
    [[ "$context" == *"gcp"* ]] || \
    [[ "$context" == *"azure"* ]]); then
  export YBA_ADDITIONAL_VALUES_FILE="./additional-app-conf.yaml"
else
  export YBA_ADDITIONAL_VALUES_FILE="/dev/null"
fi

echo "Successfully configured the enviornment for $context"
