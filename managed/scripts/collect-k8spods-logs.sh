#!/bin/bash

# Assuming kubectl installed.

# Usage
# ./collect-k8spods-logs.sh NAMESPACE NumFilesTobeCollectedForEachPod KUBECONFIGPATH
# Namspace is mandatory argument that need to be passed.
# The remaining args are optional and used default values in case they are missing.
# Ex: ./collect-k8spods-logs.sh yb-admin-gjalla 2 ~/.kube/config

# Read cmd line args.
export NAMESPACE=$1
export NUMFILES=${2:-2} # collect latest 2 files by default per pod.
export KUBECONFIGPATH=${3:-$KUBECONFIG}

# Check if kubeconfig exists.
if [ ! -f "$KUBECONFIGPATH" ]; then
    echo "$KUBECONFIGPATH does not exist."
    exit 1
fi

# Check if namespace exists and we can access it.
namespaceStatus=$(kubectl get namespace \
    --kubeconfig="$KUBECONFIGPATH" "$NAMESPACE" -o jsonpath='{.status.phase}')
if [ "$namespaceStatus" == "Active" ]
then
    echo "namespace $NAMESPACE found."
else
   echo "namespace $NAMESPACE is not found or active."
   exit 1
fi

# Get pods in the namespace and extract master/tserver pod names.
masterpods=$(kubectl get pod -n "$NAMESPACE" --kubeconfig="$KUBECONFIGPATH" \
    -l app=yb-master -o custom-columns=:metadata.name)
tserverpods=$(kubectl get pod -n "$NAMESPACE" --kubeconfig="$KUBECONFIGPATH" \
    -l app=yb-tserver -o custom-columns=:metadata.name)
echo "master pods found: $masterpods"
echo "tserver pods found: $tserverpods"

# Create temp directory for storing logs.
outDir="./$NAMESPACE-$(date "+%s")"
mkdir "$outDir"

# Loop through all pods.
    # Create a namespace/podname directory inside temp directory.
    # Exec into yb-cleanup container and get log file names.
    # Copy at most latest $NUMFILES files into above directory.
IFS=$(echo -en "\n\b") read -ra PODS <<< $masterpods # remove new lines.
IFS=' ' read -ra masterpod <<< "$PODS" # split pods.
for pod in "${masterpod[@]}"; do
    echo "Process log files from $pod"
    mkdir "$outDir"/"$pod"
    kubectl exec -n "$NAMESPACE" "$pod" -c yb-cleanup --kubeconfig="$KUBECONFIGPATH" -- bash -c \
        "ls -t /home/yugabyte/master/logs/yb-master*INFO?*" | head -n "$NUMFILES" | \
        while read filepath; do
            kubectl --kubeconfig="$KUBECONFIGPATH" cp "$NAMESPACE"/"$pod":"$filepath" \
                "$outDir"/"$pod"/"${filepath##*/}" -c yb-cleanup;
        done
    # ${filepath##*/} will split the whole path of file using '/' and take last word ie. file name.
done

IFS=$(echo -en "\n\b") read -ra PODS <<< $tserverpods # remove new lines.
IFS=' ' read -ra tserverpod <<< "$PODS" # split pods.
for pod in "${tserverpod[@]}"; do
    echo "Process log files from $pod"
    mkdir "$outDir"/"$pod"
    # INFO files
    kubectl exec -n "$NAMESPACE" "$pod" -c yb-cleanup --kubeconfig="$KUBECONFIGPATH" -- bash -c \
        "ls -t /home/yugabyte/tserver/logs/yb-tserver*INFO?*" | head -n "$NUMFILES" | \
        while read filepath; do
            kubectl --kubeconfig="$KUBECONFIGPATH" cp "$NAMESPACE"/"$pod":"$filepath" \
                "$outDir"/"$pod"/"${filepath##*/}" -c yb-cleanup;
        done
    # postgres files
    kubectl exec -n "$NAMESPACE" "$pod" -c yb-cleanup --kubeconfig="$KUBECONFIGPATH" -- bash -c \
        "ls -t /home/yugabyte/tserver/logs/postgres*" | head -n "$NUMFILES" | \
        while read filepath; do
            kubectl --kubeconfig="$KUBECONFIGPATH" cp "$NAMESPACE"/"$pod":"$filepath" \
                "$outDir"/"$pod"/"${filepath##*/}" -c yb-cleanup;
        done
done

# Create tar out of temp directory.
tar czf "$outDir".tar.gz ./"$outDir"
rm -rf "$outDir"
echo "Logs are collected in $outDir.tar.gz"

exit 0
