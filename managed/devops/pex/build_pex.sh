#!/usr/bin/env bash

set -e

. ../bin/common.sh

# Environment variable to control the level of PEX logging printed to the console
# when generating the PEX. Increase the number if you want more verbose logging output
# (Configurable on a scale from 1 to 10).
export PEX_VERBOSE=2

# Faster creation of yb_cassandra_driver wheels without prebuilt compilation.
export CASS_DRIVER_NO_CYTHON=1

# Used for function return values to make execution clear in main.
POINTER=""

PYTHON_REQUIREMENTS_FILE=""

# Generate the PEX file that support multiple platforms and Python Versions.
# Currently supports all Linux Platforms newer than manylinux_2014, and all Python
# Versions from 3.6 to 3.11. macOS support might also be added in the future.
function generateMultiPlatformPex {

    echo "Generating the PEX file ... "
    # Executable command to generate the PEX file. Components:
    # Line 1: Required Python dependencies (python3_requirements_frozen.txt)
    # Line 2: Required Python versions (3.6, 3.7, 3.8, 3.9, 3.10, 3.11)
    # Line 3: Required Linux Platforms (manyLinux2014, one for each Python version)
    # Line 4: Data directories to bundle into the PEX (opscli)
    # Line 5: --resolve-local-platforms flag (Ensure wheels built in the pex match
    # the platform specifications)
    # Line 6: Created PEX file output (named as pexEnv.pex)
    pex_command_exec=(python3 -m pex -r "$PYTHON_REQUIREMENTS_FILE"
    ${PYTHON3_VERSIONS[@]/#/--python=}
    ${LINUX_PLATFORMS[@]/#/--platform=}
    -D ../opscli
    --resolve-local-platforms
     -o pexEnv.pex)

    ${pex_command_exec[*]}
}

# Repair the manylinux2014_x86_64 wheels present in the PEX file using auditwheel.
# (extract the created PEX into a folder in order to do so). Note that macOS wheels
# are not currently packaged into the created PEXfile, but might be so in the
# future).
function repairPexWheels {
    pexFile=$POINTER
    unzip $pexFile -d pexEnv
    cd pexEnv/.deps
    mkdir extractedWheels
    for whl in *; do
        if [ $whl != "extractedWheels" ]; then
            wheel pack $whl -d extractedWheels
        fi
        done
    cd extractedWheels
    echo "Repairing wheels using auditwheel"
    for whl in *; do
        if ! auditwheel show "$whl"; then
            if [[ $whl =~ "-macosx_" ]]; then
                echo "macOS X wheel $whl, already repaired in pex_wheels_macos.sh!"
            else
                echo "Skipping non-platform wheel $whl"
            fi
    else
        auditwheel repair --plat manylinux2014_x86_64 $whl --no-update-tags
    fi
done
}

# Reconstruct the repaired manylinux2014_x86_64 wheels back into the PEX
# by using wheel unpack and renaming the wheel directories.
function reconstructPex {
    extractedWheels=$POINTER
    echo "Reconstructing the repaired wheels into a PEX"
    mv wheelhouse ..
    cd ..
    rm -rf $extractedWheels
    cd wheelhouse

    # At a high level, for each wheel repaired by the Auditwheel tool, this
    # function first removes the wheel directory already present in the PEX
    # folder, which corresponds to the unrepaired wheel created during the initial
    # generation of the PEX.
    for whl in *; do
        rm -rf "../${whl}"
    done
    # This function then uses the wheel unpack tool to unpack the repaired wheel from
    # a .whl file into a .whl folder, and places that .whl folder into the
    # PEX folder. This is a replacement of the wheel directories that corresponds to using
    # the repaired auditwheel .whl folders instead of the original .whl folders.
    for whl in *; do
        wheel unpack $whl -d "../${whl}"
    done
    # Finally, to complete the process of reconstructing the PEX, we note that
    # the auditwheel tool repairs each module wheel and places it into a
    # separate folder with the module name, but the PEX folder operates on the
    # direct module wheels. So, for each repaired module .whl folder in the PEX,
    # this function moves that folder out of the module name folder, and deletes
    # that module name folder so that the PEX is able to be used.
    for whl in *; do
        splitwhl=(${whl//-/ })
        module_name=${splitwhl[0]}-${splitwhl[1]}
        mv ../${whl}/${module_name}/{.[!.],}* "../${whl}"
        rm -rf "../${whl}/${module_name}"
    done
    cd ..
    rm -rf wheelhouse
    cd /code/pex/
    rm -rf pexEnv.pex

}

# Function to verify that the repaired PEX folder satisfies all module imports
# without any missing third-party shared library issues.
function testRepairedPex {
    pexEnv=$POINTER
    echo "Testing that the repaired PEX folder works and imports all modules."
    python3 $pexEnv /code/pex/pexEnvTest.py -r "$PYTHON_REQUIREMENTS_FILE"
}

FOLDER="pexEnv"

print_help() {
  cat <<-EOT
Builds pex env for a provided requirements file.
Usage: ${0##*/} <options>
Options:
  -h, --help
    Show usage.
  -r, --requirements
    Python requirements file for building pex env.
EOT
}

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    -r|--requirements)
      PYTHON_REQUIREMENTS_FILE="$2"
      shift
    ;;
  esac
  shift
done

if [[ $PYTHON_REQUIREMENTS_FILE == "" ]]; then
  PYTHON_REQUIREMENTS_FILE="../python3_requirements_frozen.txt"
  echo "No python requirements file provided. Using the default requirements file."
fi

if [ ! -d "$FOLDER" ]; then
    # pexEnv.pex is returned by generateMultiPlatformPex (original PEX file), which
    # is then used as the input to repair pexWheels.
    generateMultiPlatformPex
    POINTER="pexEnv.pex"

    # extractedWheels is returned by repairPexWheels (directory of repaired
    # wheels), which is then used as the input to reconstructPex.
    repairPexWheels
    POINTER="extractedWheels"

    # pexEnv is returned by reconstructPex (repaired PEX folder), which
    # is then used as the input to testRepairedPex.
    reconstructPex
    POINTER="pexEnv"

    testRepairedPex
else
  echo "$FOLDER already generated, skipping recreation."
fi
