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
base vectors, queries and ground truth. Options of this script go before the dataset name. The
arguments after it go to hnsw_tool, and override the dataset's files or distance kind. With any
of them overridden, or --num_vectors_to_insert given, hnsw_tool computes the ground truth unless
--ground_truth_path is given.

Examples:
  bin/vi_benchmark.py siftsmall
  bin/vi_benchmark.py --cache_dir ~/vi_cache sift --ann_method kHnswlib --ef_values 64,128 --k 10
"""

import argparse
import glob
import gzip
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import zlib
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Set


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CACHE_DIR = os.path.join(REPO_ROOT, 'build', 'vi_cache')
# curl's exit code when the partial file does not fit the remote file.
CURL_BAD_DOWNLOAD_RESUME = 36


@dataclass
class Dataset:
    # A .tar.gz archive that contains the files below.
    url: str
    base_file: str
    query_file: str
    ground_truth_file: str
    distance_kind: str = 'kL2Squared'

    def files(self) -> List[str]:
        return [self.base_file, self.query_file, self.ground_truth_file]


def texmex(name: str) -> Dataset:
    return Dataset(
        url=f'ftp://ftp.irisa.fr/local/texmex/corpus/{name}.tar.gz',
        base_file=f'{name}_base.fvecs',
        query_file=f'{name}_query.fvecs',
        ground_truth_file=f'{name}_groundtruth.ivecs')


# TEXMEX (Inria): siftsmall has 10K 128-dimensional base vectors, sift 1M, and gist 1M
# 960-dimensional ones.
DATASETS = {name: texmex(name) for name in ('siftsmall', 'sift', 'gist')}


def default_tool() -> Optional[str]:
    # Times measured with a debug or sanitizer build are not representative.
    tools = glob.glob(os.path.join(REPO_ROOT, 'build', 'release-*', 'bin', 'hnsw_tool'))
    return max(tools, key=os.path.getmtime) if tools else None


def given_options(options: Iterable[str], tool_args: List[str]) -> Set[str]:
    names = [arg.split('=', 1)[0] for arg in tool_args if arg.startswith('--')]
    # hnsw_tool also takes an unambiguous prefix of an option name.
    return {option for option in options
            for name in names if len(name) > 2 and option.startswith(name)}


def download(url: str, path: str) -> None:
    print(f'Downloading {url}', flush=True)
    partial = path + '.part'
    # --continue-at - resumes a download that an earlier run left in the .part file.
    cmd = ['curl', '--fail', '--location', '--show-error', '--continue-at', '-', '--output',
           partial, url]
    returncode = subprocess.call(cmd)
    if returncode == CURL_BAD_DOWNLOAD_RESUME:
        os.remove(partial)
        returncode = subprocess.call(cmd)
    if returncode != 0:
        sys.exit(f'Failed to download {url}: curl exit code {returncode}')
    os.rename(partial, path)


def extract(archive: str, dataset_dir: str, names: List[str]) -> None:
    print(f'Extracting {archive}', flush=True)
    # Renamed once the whole archive is checked, so an existing file is never partial or corrupt.
    parts: Dict[str, str] = {}
    try:
        with gzip.open(archive) as stream:
            # Writes only the named files, so the paths in the archive don't matter.
            with tarfile.open(fileobj=stream, mode='r|') as tar:
                for member in tar:
                    name = os.path.basename(member.name)
                    if member.isfile() and name in names and name not in parts:
                        parts[name] = os.path.join(dataset_dir, name + '.part')
                        with open(parts[name], 'wb') as output:
                            shutil.copyfileobj(tar.extractfile(member), output)
            # gzip checks the CRC of the data at its end. A corrupt archive need not fail before
            # that: tarfile takes a garbled header for the end of the archive.
            while stream.read(1 << 20):
                pass
    except (tarfile.TarError, EOFError, zlib.error, gzip.BadGzipFile) as e:
        for part in parts.values():
            os.remove(part)
        os.remove(archive)
        sys.exit(f'Failed to read {archive}, deleted it, rerun to download it again: {e}')
    missing = set(names) - parts.keys()
    if missing:
        for part in parts.values():
            os.remove(part)
        # The archive passed the CRC check, so downloading it again would not help.
        sys.exit(f'{archive} has no {", ".join(sorted(missing))}; kept the archive')
    for name, part in parts.items():
        os.replace(part, os.path.join(dataset_dir, name))


def prepare(dataset: Dataset, dataset_dir: str, needed: List[str]) -> None:
    if all(os.path.exists(os.path.join(dataset_dir, name)) for name in needed):
        return
    # All missing files are extracted, so a run with other arguments does not download again.
    missing = [name for name in dataset.files()
               if not os.path.exists(os.path.join(dataset_dir, name))]
    os.makedirs(dataset_dir, exist_ok=True)
    archive = os.path.join(dataset_dir, os.path.basename(dataset.url))
    if not os.path.exists(archive):
        download(dataset.url, archive)
    extract(archive, dataset_dir, missing)
    os.remove(archive)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        # Only the full names of the options below are rejected after the dataset name.
        allow_abbrev=False)
    parser.add_argument('--cache_dir', default=DEFAULT_CACHE_DIR,
                        help='Directory the datasets are downloaded to (default: %(default)s).')
    parser.add_argument('--tool', help='hnsw_tool binary (default: the newest release build).')
    parser.add_argument('--dry_run', action='store_true',
                        help='Print the hnsw_tool command without downloading or running anything.')
    parser.add_argument('dataset', help=f'One of {", ".join(sorted(DATASETS))}.')
    parser.add_argument('tool_args', nargs=argparse.REMAINDER,
                        help='Arguments for hnsw_tool benchmark.')
    # Unknown options before the dataset name are returned, not taken as hnsw_tool arguments.
    args, misplaced = parser.parse_known_args()
    if misplaced:
        parser.error(f'{misplaced[0]}: hnsw_tool arguments go after the dataset name')
    # hnsw_tool has no options with these names.
    for name in [arg.split('=', 1)[0] for arg in args.tool_args]:
        if name in ('--cache_dir', '--tool', '--dry_run'):
            parser.error(f'{name}: options of this script go before the dataset name')
    dataset = DATASETS.get(args.dataset)
    if dataset is None:
        parser.error(f'unknown dataset {args.dataset}; use one of {", ".join(sorted(DATASETS))}')
    tool = args.tool or default_tool()
    if tool is None:
        sys.exit('No release build of hnsw_tool; build it with '
                 '"./yb_build.sh release --target hnsw_tool", or pass --tool')
    if shutil.which(tool) is None:
        sys.exit(f'{tool}: no such executable')

    dataset_dir = os.path.join(args.cache_dir, args.dataset)
    dataset_files = {
        '--build_vecs_path': dataset.base_file,
        '--query_vecs_path': dataset.query_file,
        '--ground_truth_path': dataset.ground_truth_file,
    }
    dataset_args = {option: os.path.join(dataset_dir, name)
                    for option, name in dataset_files.items()}
    dataset_args['--distance_kind'] = dataset.distance_kind
    overridden = given_options(dataset_args, args.tool_args)
    if overridden or given_options(['--num_vectors_to_insert'], args.tool_args):
        # The dataset's ground truth does not fit other or fewer base vectors, other queries or
        # another distance kind, so hnsw_tool computes it, unless --ground_truth_path is given too.
        overridden.add('--ground_truth_path')
    cmd = [tool, 'benchmark']
    for option, value in dataset_args.items():
        # hnsw_tool rejects an option given twice.
        if option not in overridden:
            cmd += [option, value]
    cmd += args.tool_args
    print('+ ' + shlex.join(cmd), flush=True)
    if args.dry_run:
        return 0
    needed = [name for option, name in dataset_files.items() if option not in overridden]
    if needed:
        prepare(dataset, dataset_dir, needed)
    return subprocess.call(cmd)


if __name__ == '__main__':
    sys.exit(main())
