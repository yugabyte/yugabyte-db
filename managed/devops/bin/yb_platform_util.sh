#!/usr/bin/env bash
#
# Copyright (c) YugaByte, Inc.
#
# Copyright 2021 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt


bold="\033[1m"
normal="\033[0m"
success="success"

function ProgressBar {
# Process data
    let _progress=(${1}*100/${2}*100)/100
    let _done=(${_progress}*4)/10
    let _left=40-$_done
# Build progressbar string lengths
    _fill=$(printf "%${_done}s")
    _empty=$(printf "%${_left}s")

printf "\rProgress : [${_fill// /#}${_empty// /-}] ${_progress}%%\n"

}

function TaskStatus {
    task_id=$1
    base_url=$(echo "$YB_PLATFORM_URL" | sed 's/\/$//')
    auth_uuid="$YB_PLATFORM_API_TOKEN"
    customer=$2
    success_message=$3
    _end=100
    python_command="python"
    if [[ "$(python3 -V)" =~ "Python 3" ]]
    then
        python_command="python3"
    fi

    while :
    do
        percentage_response=$($python_command -c "import yb_platform_util; \
          yb_platform_util.get_task_details('$task_id', '$base_url', '$customer', \
          '$auth_uuid',)")
        new_percentage_response=$(echo $percentage_response | sed "s/'/\"/g")
        percentage_status=$($python_command -c "import yb_platform_util; \
          yb_platform_util.get_key_value('$new_percentage_response', 'status')")
        percentage=$($python_command -c "import yb_platform_util; \
          yb_platform_util.get_key_value('$new_percentage_response', 'data')")

        if [[ ! $percentage_status == $success ]]
        then
            echo $percentage
            exit
        fi

        if [[ -n "$percentage" ]] && [[ $percentage =~ ^[0-9]+$ ]] && [[ "$percentage" -ge 0 ]] \
          && [[ "$percentage" -le 100 ]]
        then
            ProgressBar ${percentage} ${_end}
            if [[ $percentage -eq 100 ]]
            then
                printf "\n$success_message\n"
                break
            fi
            sleep 20
        else
            printf "${percentage}\n"
            break
        fi
    done
}
function HelpMessage {

cat << EOF
Script to perform universe actions.
        1. Get existing universe list. 
        2. Get existing universe details by universe name in json format.
        3. Get existing universe details by universe UUID in json format.
        4. Delete existing universe by universe name.
        5. Delete existing universe by universe UUID.
        6. Create a new universe from a json config file.
        7. Get task progress. 
        8. Get list of available regions with availability zones. 
        9. Get list of available providers. 

Required to export variable:
YB_PLATFORM_URL 
         API URL for yugabyte
         Example: 
                 export YB_PLATFORM_URL=http://localhost:9000
YB_PLATFORM_API_TOKEN 
         API token for Yugabyte API
         Example: 
                 export YB_PLATFORM_API_TOKEN=e16d75cf-79af-4c57-8659-2f8c34223551

Syntax: 
         bash yb_platform_util.sh <action> [params]
         Example: 
                bash yb_platform_util.sh get_universe -n test-universe

Actions:
get_universe 
         Get the details of an existing universe as a json file
         Example: 
                bash yb_platform_util.sh get_universe

         Universe json with universe name: 
                bash yb_platform_util.sh get_universe -n test-universe

add_universe |  create_universe
         Create universe from json file
         Example: 
                bash yb_platform_util.sh create_universe -f test-universe.json -n test-universe-by-name

del_universe |  delete_universe
         Delete an existing universe
         Example: 
                bash yb_platform_util.sh delete_universe -n test-universe

task_status 
         To get task status
         Example: 
                bash yb_platform_util.sh task_status -t f33e3c9b-75ab-4c30-80ad-cba85646ea39

get_provider 
         To get list of available providers
         Example: 
                bash yb_platform_util.sh get_provider

get_region | get_az 
         List of available region with availability zones
         Example: 
                bash yb_platform_util.sh get_region

Params:

-c | --customer_uuid 
         Customer UUID; mandatory if multiple customer uuids present
         Example:
                bash yb_platform_util.sh get_universe -c f8d2490a-f07c-4a40-a523-767f4f3b12da

-u | --universe_uuid 
         Universe UUID
         Example:
                bash yb_platform_util.sh get_universe -u f8d2490a-f07c-4a40-a523-767f4f3b12da

-n | --universe_name 
         Universe name
         Example:
                bash yb_platform_util.sh get_universe -n test-universe

-f | --file 
         Json input/output file for creating universe
         Example: 
                bash yb_platform_util.sh create_universe -f test-universe.json -n test-universe-by-name

-y | --yes 
         Input yes for all confirmation prompts
         Example: 
                bash yb_platform_util.sh delete_universe -n test-universe-by-name -y

--no-wait 
         To run command in background and do not wait for task completion task
         Example: 
                bash yb_platform_util.sh create_universe -f test-universe.json --no-wait

-t | --task 
         Task UUID to get task status
         Example: 
                bash yb_platform_util.sh task_status -t f33e3c9b-75ab-4c30-80ad-cba85646ea39

--force 
         Force delete universe
         Example: 
                bash yb_platform_util.sh create_universe -force test-universe.json

-h | --help 
         Print help message
         Example: 
                 bash yb_platform_util.sh -h
EOF
}

USER_DIR=$(pwd)
BASEDIR=$(dirname "$0")
cd "$BASEDIR/lib"

if [[ -n "$1" ]]
then
    case $1 in
        -h|--help)
            help="True"
            shift # argument
            ;;
        *)
            operation=$(echo "$1" | tr '[:lower:]' '[:upper:]')
            shift # argument
            ;;
    esac
else
    echo "Error: Invalid Actions."
    HelpMessage
    exit
fi

# Loop to fetch all the values for the user
# Example, sh yb_platform_util.sh -c f33e3c9b-75ab-4c30-80ad-cba85646ea39 -n jd-script-8-2-21 -o GET
# this will set customer id name and opertation as get.
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -c|--customer_uuid)
            customer_uuid="$2"
            shift 2 #shift argument and value
            ;;
        -u|--universe_uuid)
            universe_uuid="$2"
            shift 2 #shift argument and value
            ;;
        -n|--universe_name)
            universe_name="$2"
            shift 2 #shift argument and value
            ;;
        -f|--file)
            input_file="$USER_DIR/$2"
            shift 2 #shift argument and value
            ;;
        -h|--help)
            help="True"
            shift # argument
            ;;
        -y|--yes)
            confirmation="y"
            shift # argument
            ;;
        --no-wait)
            skip_wait="y"
            shift # argument
            ;;
        --force)
            force_delete="y"
            shift # argument
            ;;
        -t|--task)
            task_id="$2"
            shift 2 #shift argument and value
            ;;
        *)
            echo "Error: Invalid command line argument"
            HelpMessage
            exit 255
            ;;
    esac
done


# Checking if system has python3 installed.
# If python3 installed run script using python 3 use 2 otherwise.

python_command="python"
if [[ "$(python3 -V)" =~ "Python 3" ]]
then
    python_command="python3"
fi

# Help message for the users.
if [[ -n "$help" ]] && [[ $help == "True" ]]
then
    HelpMessage
    exit 255
fi

# Validation to set YB_PLATFORM_URL and YB_PLATFORM_API_TOKEN UUID
if [[ -z "$YB_PLATFORM_URL" ]]
then
    printf "YB_PLATFORM_URL is not set. Set YB_PLATFORM_URL as env variable to proceed.\n"
    printf "\n\texport YB_PLATFORM_URL=<base_url_of_platform>\n\nExample:"
    printf "\n\texport YB_PLATFORM_URL=http://localhost:9000\n"
    exit 255
fi

if [[ -z "$YB_PLATFORM_API_TOKEN" ]]
then
    printf "YB_PLATFORM_API_TOKEN is not set. Set YB_PLATFORM_API_TOKEN as env variable to proceed.\n"
    printf "\n\texport YB_PLATFORM_API_TOKEN=<platform_api_token>\n\nExample:"
    printf "\n\t export YB_PLATFORM_API_TOKEN=e16d75cf-79af-4c57-8659-2f8c34223551\n"
    exit 255
fi

base_url=$(echo "$YB_PLATFORM_URL" | sed 's/\/$//')
auth_uuid="$YB_PLATFORM_API_TOKEN"

if [[ -z "$customer_uuid" ]]
then
    customer_response=$($python_command -c "import yb_platform_util;  \
      yb_platform_util.get_single_customer_uuid('$base_url', '$auth_uuid')")
    new_customer_response=$(echo $customer_response | sed "s/'/\"/g")
    customer_uuid_status=$($python_command -c "import yb_platform_util;  \
      yb_platform_util.get_key_value('$new_customer_response', 'status')")
    customer_uuid=$($python_command -c "import yb_platform_util;  \
      yb_platform_util.get_key_value('$new_customer_response', 'data')")

    if [[ ! $customer_uuid_status == $success ]]
    then
        error_message=$($python_command -c "import yb_platform_util;  \
          yb_platform_util.get_key_value('$new_customer_response', 'error')")
        echo $error_message
        exit
    fi
fi

# Operation can be add/create/get/delete/del all are canse insensitive.
# Add/create will call python api to create universe
# Get will fetch universe details using name/universe UUID.
# Delete Will delete universe by name/universe UUID
case $operation in
    ADD_UNIVERSE|CREATE_UNIVERSE)
        if [[ -z "$input_file" ]]
        then
            printf "Input json file required. Use \`-f|--file <file_path>\` to "
            printf "pass json input file.\n"
            exit 255
        fi

        universe_reponse=$($python_command -c "import yb_platform_util; \
          yb_platform_util.create_universe('$base_url', '$customer_uuid', '$auth_uuid', \
          '$input_file', '$universe_name')")
        new_universe_reponse=$(echo $universe_reponse | sed "s/'/\"/g")
        universe_status=$($python_command -c "import yb_platform_util; \
          yb_platform_util.get_key_value('$new_universe_reponse', 'status')")
        task_id=$($python_command -c "import yb_platform_util; \
          yb_platform_util.get_key_value('$new_universe_reponse', 'data')")

        if [[ ! $universe_status == $success ]]
        then
            echo $task_id
            exit
        fi
        if [[ -n "$skip_wait" ]]
        then
            echo "Universe create requested successfully. "
            echo "Use $task_id as task id to get status of universe."
        else
            echo "Universe create requested successfully. "
            echo "Use $task_id as task id to get status of universe."
            TaskStatus ${task_id} ${customer_uuid} "Universe creation successfully."
        fi
        ;;
    GET_UNIVERSE)
        if [[ -n "$universe_uuid" ]]
        then
            result=$($python_command -c "import yb_platform_util; \
              yb_platform_util.save_universe_details_to_file_by_uuid('$base_url', \
              '$customer_uuid', '$auth_uuid', '$universe_uuid', '$USER_DIR')")
            echo $result
        elif [[ -n "$universe_name" ]]
        then
            result=$($python_command -c "import yb_platform_util; \
              yb_platform_util.save_universe_details_to_file('$base_url', '$customer_uuid', \
              '$auth_uuid', '$universe_name', '$USER_DIR')")
            echo $result
        else
          result=$($python_command -c "import yb_platform_util; \
            yb_platform_util.get_universe_list('$base_url', '$customer_uuid', \
            '$auth_uuid')")
          echo $result
          echo ""
        fi
        ;;
    DELETE_UNIVERSE|DEL_UNIVERSE)
        if [[ -z "$universe_uuid" ]] && [[ -z "$universe_name" ]]
        then
            printf "Required universe name | uuid to delete universe.\n"
            printf "Use \`-n|--universe_name <universe_name>\` to pass universe name.\n"
            printf "Use \`-u|--universe_uuid <universe_uuid>\` to pass universe uuid.\n"
            exit 255
        fi
        if [[ -z "$confirmation" ]]
        then
            read -p "Continue with deleting universe(y/n)? :" confirmation
            echo
        fi

        if [[ $confirmation =~ ^[yY] ]]; 
        then
            if [[ -z "$universe_uuid" ]] && [[ -n "$universe_name" ]]
            then
                result=$($python_command -c "import yb_platform_util; \
                  yb_platform_util.get_universe_uuid_by_name('$base_url', '$customer_uuid', \
                  '$auth_uuid', '$universe_name')")
                new_result=$(echo $result | sed "s/'/\"/g")
                res=$($python_command -c "import yb_platform_util; \
                  yb_platform_util.get_key_value('$new_result', 'status')")
                universe_uuid=$($python_command -c "import yb_platform_util; \
                  yb_platform_util.get_key_value('$new_result', 'data')")
                if [[ ! $res == $success ]]
                then
                    echo $universe_uuid
                    exit
                fi
            fi

            task_result=$($python_command -c "import yb_platform_util; \
              yb_platform_util.delete_universe_by_id('$base_url', '$customer_uuid', \
              '$auth_uuid', '$universe_uuid', '$force_delete')")
            new_task_result=$(echo $task_result | sed "s/'/\"/g")
            task_result_status=$($python_command -c "import yb_platform_util; \
              yb_platform_util.get_key_value('$new_task_result', 'status')")
            task_id=$($python_command -c "import yb_platform_util; \
              yb_platform_util.get_key_value('$new_task_result', 'data')")
            if [[ ! $task_result_status == $success ]]
            then
                echo $task_id
                exit
            fi
            if [[ -n "$force_delete" ]]
            then
                echo "Note:- Universe deletion can fail due to errors, Use `--force` to ignore errors and force delete."
            fi

            if [[ -n "$skip_wait" ]]
            then
                echo "Universe delete requested successfully."
                echo "Use $task_id as task id to get status of universe."
            else
                echo "Universe delete requested successfully."
                echo "Use $task_id as task id to get status of universe."
                TaskStatus ${task_id} ${customer_uuid} "Universe Deletion successfully."
            fi
        else
            echo "Aborted Delete universe"
        fi
        ;;
    TASK_STATUS)
        if [[ -z "$task_id" ]]
        then
            echo "Task id required to get task status."
            printf " Use \`-t|--task <task_id>\` to pass task_id.\n"
            exit
        fi
        TaskStatus ${task_id} ${customer_uuid} "Task completed successfully."
        ;;
    GET_PROVIDER)
      result=$($python_command -c "import yb_platform_util; \
        yb_platform_util.get_provider_data('$base_url', '$customer_uuid', \
        '$auth_uuid')")
      echo $result
      echo
      ;;
    GET_REGION | GET_AZ)
        result=$($python_command -c "import yb_platform_util; \
          yb_platform_util.get_regions_data('$base_url', '$customer_uuid', \
          '$auth_uuid')")
        echo $result
        echo
      ;;
    *)
        echo "Invalid opertaion"
        HelpMessage
        exit 255
        ;;

esac
