#!/usr/bin/env bash
set -ue -o pipefail

readonly BASEDIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
(
cd "${BASEDIR}"
readonly APISERVERDIR=${BASEDIR}/apiserver/cmd/server
readonly UIDIR=${BASEDIR}/ui
readonly OUTDIR="${BUILD_ROOT:-/tmp/yugabyted-ui}/gobin"
readonly OUTFILE="${OUTDIR}/yugabyted-ui"
mkdir -p "${OUTDIR}"

if ! command -v npm -version &> /dev/null
then
  echo "npm could not be found"
  exit
fi

if ! command -v go version &> /dev/null
then
  echo "go lang could not be found"
  exit
fi

(
cd $UIDIR
npm ci
npm run build
tar cz ui | tar -C "${APISERVERDIR}" -xz
)

cd $APISERVERDIR
go build -o "${OUTFILE}"

if [[ -f "${OUTFILE}" ]]
then
  echo "Yugabyted UI Binary generated successfully at ${OUTFILE}"
else
  echo "Build Failed."
fi
)
