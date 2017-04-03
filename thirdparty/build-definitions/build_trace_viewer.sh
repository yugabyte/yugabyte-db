# Copyright (c) YugaByte, Inc.

# Kudu's trace-viewer repository is separate since it's quite large and shouldn't change frequently.
# They upload the built artifacts (HTML/JS) when we need to roll to a new revision.
# TODO: see if we actually need this.
#
# The source can be found at https://github.com/cloudera/kudu-trace-viewer
# and built with "kudu-build.sh" included within the repository.
TRACE_VIEWER_VERSION=45f6525d8aa498be53e4137fb73a9e9e036ce91d
TRACE_VIEWER_DIR=$TP_SOURCE_DIR/kudu-trace-viewer-${TRACE_VIEWER_VERSION}
TP_NAME_TO_SRC_DIR["trace_viewer"]=$TRACE_VIEWER_DIR

build_trace_viewer() {
  log "Installing trace-viewer into the www directory"
  (
    set_build_env_vars
    set -x
    cp -a $TRACE_VIEWER_DIR/* $TP_DIR/../www/
  )
}
