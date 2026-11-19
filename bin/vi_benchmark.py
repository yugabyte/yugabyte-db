#!/usr/bin/env python3
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

"""
Run hnsw_tool benchmark on a public vector dataset.

Downloads the dataset into the cache directory on first use, then runs hnsw_tool benchmark on its
base vectors, queries and ground truth. Arguments this script does not recognize go to hnsw_tool.

Examples:
  bin/vi_benchmark.py siftsmall
  bin/vi_benchmark.py sift --ann_method kHnswlib --ef_values 64,128,256 --k 10,100
"""

import argparse
import os
import shlex
import subprocess
import sys
import tarfile
from dataclasses import dataclass


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CACHE_DIR = os.path.join(REPO_ROOT, 'build', 'vi_cache')
DEFAULT_TOOL = os.path.join(REPO_ROOT, 'build', 'latest', 'bin', 'hnsw_tool')


@dataclass
class Dataset:
    # A .tar.gz archive with the files below, relative to the dataset's cache directory.
    url: str
    base_file: str
    query_file: str
    ground_truth_file: str
    distance_kind: str = 'kL2Squared'


def texmex(name: str) -> Dataset:
    return Dataset(
        url=f'ftp://ftp.irisa.fr/local/texmex/corpus/{name}.tar.gz',
        base_file=f'{name}/{name}_base.fvecs',
        query_file=f'{name}/{name}_query.fvecs',
        ground_truth_file=f'{name}/{name}_groundtruth.ivecs')


# TEXMEX (Inria): siftsmall has 10K 128-dimensional base vectors, sift 1M, and gist 1M
# 960-dimensional ones.
DATASETS = {name: texmex(name) for name in ('siftsmall', 'sift', 'gist')}


def prepare(dataset: Dataset, dataset_dir: str) -> None:
    archive = os.path.join(dataset_dir, os.path.basename(dataset.url))
    # Written once the archive is fully extracted, so an interrupted run extracts it again.
    marker = archive + '.extracted'
    if os.path.exists(marker):
        return
    os.makedirs(dataset_dir, exist_ok=True)
    if not os.path.exists(archive):
        print(f'Downloading {dataset.url}', flush=True)
        partial = archive + '.part'
        # --continue-at - resumes a download that an earlier run left in the .part file.
        subprocess.run(
            ['curl', '--fail', '--location', '--show-error', '--continue-at', '-',
             '--output', partial, dataset.url],
            check=True)
        os.rename(partial, archive)
    print(f'Extracting {archive}', flush=True)
    with tarfile.open(archive) as tar:
        tar.extractall(dataset_dir, filter='data')
    open(marker, 'w').close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        # Otherwise a prefix of one of these options would take an hnsw_tool option.
        allow_abbrev=False)
    parser.add_argument('dataset', choices=sorted(DATASETS))
    parser.add_argument('--cache_dir', default=DEFAULT_CACHE_DIR,
                        help='Directory the datasets are downloaded to (default: %(default)s).')
    parser.add_argument('--tool', default=DEFAULT_TOOL,
                        help='hnsw_tool binary (default: %(default)s).')
    parser.add_argument('--dry_run', action='store_true',
                        help='Print the hnsw_tool command without downloading or running anything.')
    args, tool_args = parser.parse_known_args()

    dataset = DATASETS[args.dataset]
    dataset_dir = os.path.join(args.cache_dir, args.dataset)
    cmd = [
        args.tool, 'benchmark',
        '--build_vecs_path', os.path.join(dataset_dir, dataset.base_file),
        '--query_vecs_path', os.path.join(dataset_dir, dataset.query_file),
        '--ground_truth_path', os.path.join(dataset_dir, dataset.ground_truth_file),
        '--distance_kind', dataset.distance_kind,
    ] + tool_args
    print('+ ' + shlex.join(cmd), flush=True)
    if args.dry_run:
        return 0
    if not os.path.isfile(args.tool):
        sys.exit(f'hnsw_tool not found at {args.tool}; build it or pass --tool')
    # Checked before the download, which can take a long time.
    if not hasattr(tarfile, 'data_filter'):
        sys.exit('This Python lacks tarfile extraction filters, added in 3.12 and backported to '
                 '3.8.17, 3.9.17, 3.10.12 and 3.11.4')
    prepare(dataset, dataset_dir)
    return subprocess.call(cmd)


if __name__ == '__main__':
    sys.exit(main())
