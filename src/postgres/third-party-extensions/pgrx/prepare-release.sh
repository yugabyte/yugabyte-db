#! /usr/bin/env bash
#LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
#LICENSE
#LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
#LICENSE
#LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
#LICENSE
#LICENSE All rights reserved.
#LICENSE
#LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
 

NEW_VERSION=$1
if [ -z "${NEW_VERSION}" ]; then
	echo "Usage: ./prepare-release.sh NEW_VERSION_NUMBER [base branch name]"
	exit 1
fi
BASE_BRANCH=$2
if [ -z "${BASE_BRANCH}" ]; then
	echo "using develop as the base branch"
	BASE_BRANCH=develop
fi

git switch ${BASE_BRANCH} || exit $?
git fetch origin || exit $?
git diff origin/${BASE_BRANCH} | if [ "$0" = "" ]; then
    echo "git diff found local changes on ${BASE_BRANCH} branch, cannot cut release."
elif [ "$NEW_VERSION" = "" ]; then
    echo "No version set. Are you just copying and pasting this without checking?"
else
    git pull origin ${BASE_BRANCH} --ff-only || exit $?
    git switch -c "prepare-${NEW_VERSION}" || exit $?

    cargo install --path cargo-pgrx --locked || exit $?
    cargo pgrx init || exit $?

    # exit early if the script fails 
    ./update-versions.sh "${NEW_VERSION}" || exit $?

    # sanity check the diffs, but not Cargo.lock files cuz ugh
    # git diff -- . ':(exclude)Cargo.lock'

    # send it all to github
    git commit -a -m "Update version to ${NEW_VERSION}" || exit $?
    git push --set-upstream origin "prepare-${NEW_VERSION}" || exit $?
fi

