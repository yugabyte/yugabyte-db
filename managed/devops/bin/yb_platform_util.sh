# Copyright (c) YugaByte, Inc.
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt


# Save user current directory and move to shell script folder to run shell script

bold="\033[1m"
normal="\033[0m"


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
    auth_uuid="$YB_API_TOKEN"
    customer=$2
    success_message=$3
    _end=100
     python_3="False"
    if [[ "$(python3 -V)" =~ "Python 3" ]]
    then
        python_3="True"
    fi

    while :
    do

        if [[ $python_3 == "True" ]]
        then
            percentage_response=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_task_details('$task_id', '$base_url', '$customer', '$auth_uuid',)")
            new_percentage_response=$(echo $percentage_response | sed "s/'/\"/g")
            percentage_status=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_percentage_response', 'status')")
            percentage=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_percentage_response', 'data')")
        else
            percentage_response=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_task_details('$task_id', '$base_url', '$customer', '$auth_uuid',)")
            new_percentage_response=$(echo $percentage_response | sed "s/'/\"/g")
            percentage_status=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_percentage_response', 'status')")
            percentage=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_percentage_response', 'data')")
        fi

        if [[ ! $percentage_status == "success" ]]
        then
            echo $percentage
            exit
        fi

        if [[ ! -z "$percentage" ]] && [[ $percentage =~ ^[0-9]+$ ]] && [[ "$percentage" -ge 0 ]] && [[ "$percentage" -le 100 ]]
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
    printf "\n${bold}Script to perform universe actions.$normal\n"
    printf "\t1. Get existing Universe details by universe name.\n"
    printf "\t2. Get existing Universe details by universe UUID.\n"
    printf "\t3. Delete existing Universe by universe name.\n"
    printf "\t4. Delete existing Universe by universe UUID.\n"
    printf "\t5. Create Universe by universe Json.\n"
    printf "\t6. Create Universe by universe Json with overwriting universe name.\n"
    printf "\t7. Get progress of task. task can be create universe or delete universe.\n"
    printf "\n${bold}Required to export varible:$normal\n"
    printf "YB_PLATFORM_URL \n\t API URL for yugabyte,\n"
    printf "\t Example, \n\t\t export YB_PLATFORM_URL=http://localhost:9000\n"
    printf "YB_API_TOKEN \n\t API Token for Yugabyte API\n"
    printf "\t Example, \n\t\t export YB_API_TOKEN=e16d75cf-79af-4c57-8659-2f8c34223551\n"
    printf "\nSyntax: \n\t bash yb_platform_util.sh <action> [params]\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh get_universe -n test-universe\n"
    printf "\n${bold}Actions:$normal\n"
    printf "get_universe \n\t Get existing universe detail,\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh get_universe -n test-universe\n\n"
    printf "add_universe |  create_universe\n\t Create Universe from json file,\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh create_universe -f test-universe.json -n test-universe-by-name\n\n"
    printf "del_universe |  delete_universe\n\t Delete existing Universe,\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh delete_universe -n test-universe\n\n"
    printf "task_status \n\t To get task status,\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh task_status -t f33e3c9b-75ab-4c30-80ad-cba85646ea39\n\n"
    printf "\n${bold}Params:$normal\n"
    printf -- "-c | --customer_uuid \n\t Pass customer UUID, Mandatory if have multiple customers present \n"
    printf "\t Example,\n\t\tbash yb_platform_util.sh get_universe -c f8d2490a-f07c-4a40-a523-767f4f3b12da\n\n"
    printf -- "-u | --universe_uuid \n\t Pass Universe UUID,\n"
    printf "\t Example,\n\t\tbash yb_platform_util.sh get_universe -u f8d2490a-f07c-4a40-a523-767f4f3b12da\n\n"
    printf -- "-n | --universe_name \n\t Universe name,\n"
    printf "\t Example,\n\t\tbash yb_platform_util.sh get_universe -n test-universe\n\n"
    printf -- "-f | --file \n\t Input file for creating universe,\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh create_universe -f test-universe.json -n test-universe-by-name\n\n"
    printf -- "-y | --yes \n\t Input yes for all confirmation prompts,\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh delete_universe -n test-universe-by-name -y\n\n"
    printf -- "--no-wait \n\t To run command in background, not wait to complete task,\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh create_universe -f test-universe.json --no-wait\n\n"
    printf -- "-t | --task \n\t Pass Task UUID to get task status,\n"
    printf "\t Example, \n\t\tbash yb_platform_util.sh task_status -t f33e3c9b-75ab-4c30-80ad-cba85646ea39\n\n"
    printf -- "-h | --help \n\t Print help message, \n\t Example, \n\t\t bash yb_platform_util.sh -h\n\n"
    echo
}

USER_DIR=$(pwd)
BASEDIR=$(dirname "$0")
cd "$BASEDIR"

if [[ ! -z "$1" ]]
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
# unset customer_uuid
# unset universe_uuid
# unset universe_name
# unset input_file
# unset help
# unset confirmation
# unset skip_wait
# unset task_id

# Loop to fetch all the values fro the user
# Example, sh yb_platform_util.sh -c f33e3c9b-75ab-4c30-80ad-cba85646ea39 -n jd-script-8-2-21 -o GET
# this will set customer id name and opertation as get.
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -c|--customer_uuid)
            customer_uuid="$2"
            shift # argument
            shift # value
            ;;
        -u|--universe_uuid)
            universe_uuid="$2"
            shift # argument
            shift # value
            ;;
        -n|--universe_name)
            universe_name="$2"
            shift # argument
            shift # value
            ;;
        -f|--file)
            input_file="$USER_DIR/$2"
            shift # argument
            shift # value
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
        -t|--task)
            task_id="$2"
            shift # argument
            shift # value
            ;;
        *)
            echo "Error: Invalid command line argument"
            HelpMessage
            exit 255
            ;;
    esac
done


# Checking if system has python3 installed. If python3 installed run script using python 3 use 2 otherwise.

python_3="False"
if [[ "$(python3 -V)" =~ "Python 3" ]]
then
    python_3="True"
fi

# Help message for the users.
if [[ ! -z "$help" ]] && [[ $help == "True" ]]
then
    HelpMessage
    exit 255
fi

# Validation to set YB_PLATFORM_URL and YB_API_TOKEN UUID
if [[ -z "$YB_PLATFORM_URL" ]]
then
    printf "YB_PLATFORM_URL is not set. Set YB_PLATFORM_URL as env variable to proceed.\n"
    printf "\n\texport YB_PLATFORM_URL=<base_url>\n\nExample,\n\texport YB_PLATFORM_URL=http://localhost:9000\n"
    exit 255
fi

if [[ -z "$YB_API_TOKEN" ]]
then
    printf "YB_API_TOKEN is not set. Set YB_API_TOKEN as env variable to proceed.\n"
    printf "\n\texport YB_API_TOKEN=<auth_UUID>\n\nExample,\n\t export YB_API_TOKEN=e16d75cf-79af-4c57-8659-2f8c34223551\n"
    exit 255
fi

base_url=$(echo "$YB_PLATFORM_URL" | sed 's/\/$//')
auth_uuid="$YB_API_TOKEN"

if [[ -z "$customer_uuid" ]]
then
    if [[ $python_3 == "True" ]]
    then
        customer_response=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_customer_uuid('$base_url', '$auth_uuid')")
        new_customer_response=$(echo $customer_response | sed "s/'/\"/g")
        customer_uuid_status=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_customer_response', 'status')")
        customer_uuid=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_customer_response', 'data')")
    else
        customer_response=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_customer_uuid('$base_url', '$auth_uuid')")
        new_customer_response=$(echo $customer_response | sed "s/'/\"/g")
        customer_uuid_status=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_customer_response', 'status')")
        customer_uuid=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_customer_response', 'data')")
    fi

    if [[ ! $customer_uuid_status == "success" ]]
    then
        if [[ $python_3 == "True" ]]
        then
            error_message=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_customer_response', 'error')")
        else
            error_message=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_customer_response', 'error')")
        fi
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
            printf "Input json file required. Use \`-i|--input <file_path>\` to pass json input file\n"
            exit 255
        fi

        if [[ $python_3 == "True" ]]
        then
            universe_reponse=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.create_universe('$base_url', '$customer_uuid', '$auth_uuid', '$input_file', '$universe_name')")
            new_universe_reponse=$(echo $universe_reponse | sed "s/'/\"/g")
            universe_status=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_universe_reponse', 'status')")
            task_id=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_universe_reponse', 'data')")
        else
            universe_reponse=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.create_universe('$base_url', '$customer_uuid', '$auth_uuid', '$input_file', '$universe_name')")
            new_universe_reponse=$(echo $universe_reponse | sed "s/'/\"/g")
            universe_status=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_universe_reponse', 'status')")
            task_id=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_universe_reponse', 'data')")
        fi
        if [[ ! $universe_status == "success" ]]
        then
            echo $task_id
            exit
        fi
        if [[ ! -z "$skip_wait" ]]
        then
            echo "Universe create requested successfull. Use $task_id as task id to get status of universe."
        else
            echo "Universe create requested successfull. Use $task_id as task id to get status of universe."
            TaskStatus ${task_id} ${customer_uuid} "Universe Creation Successfull."
        fi
        ;;
    GET_UNIVERSE)
        if [[ ! -z "$universe_uuid" ]]
        then
            # USE python code
             if [[ $python_3 == "True" ]]
            then
                result=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_universe_details_by_uuid('$base_url', '$customer_uuid', '$auth_uuid', '$universe_uuid', '$USER_DIR')")
                echo $result
            else 
                result=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_universe_details_by_uuid('$base_url', '$customer_uuid', '$auth_uuid', '$universe_uuid', '$USER_DIR')")
                echo $result
            fi
        elif [[ ! -z "$universe_name" ]]
        then
            if [[ $python_3 == "True" ]]
            then
                result=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_universe_details('$base_url', '$customer_uuid', '$auth_uuid', '$universe_name', '$USER_DIR')")
                echo $result
            else 
                result=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_universe_details('$base_url', '$customer_uuid', '$auth_uuid', '$universe_name', '$USER_DIR')")
                echo $result
            fi
        else
            printf "Required universe name | uuid to get detail of universe.\n"
            printf "Use \`-n|--universe_name <universe_name>\` to pass universe name.\n"
            printf "Use \`-u|--universe_uuid <universe_uuid>\` to pass universe uuid.\n"
            exit 255
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
        case "$confirmation" in 
            y|Y|yes|YES ) 
                if [[ -z "$universe_uuid" ]] && [[ ! -z "$universe_name" ]]
                then
                    if [[ $python_3 == "True" ]]
                    then
                        result=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_universe_uuid('$base_url', '$customer_uuid', '$auth_uuid', '$universe_name')")
                        new_result=$(echo $result | sed "s/'/\"/g")
                        res=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_result', 'status')")
                        universe_uuid=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_result', 'data')")
                    else 
                        result=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_universe_uuid('$base_url', '$customer_uuid', '$auth_uuid', '$universe_name')")
                        new_result=$(echo $result | sed "s/'/\"/g")
                        res=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_result', 'status')")
                        universe_uuid=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_result', 'data')")
                    fi
                    if [[ ! $res == "success" ]]
                    then
                        echo $universe_uuid
                        exit
                    fi
                fi

                if [[ $python_3 == "True" ]]
                then
                    task_result=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.delete_universe_by_id('$base_url', '$customer_uuid', '$auth_uuid', '$universe_uuid')")
                    new_task_result=$(echo $task_result | sed "s/'/\"/g")
                    task_result_status=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_task_result', 'status')")
                    task_id=$(python3 -c "import yb_platform_util_py3; yb_platform_util_py3.get_key_value('$new_task_result', 'data')")
                else 
                    task_result=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.delete_universe_by_id('$base_url', '$customer_uuid', '$auth_uuid', '$universe_uuid')")
                    new_task_result=$(echo $task_result | sed "s/'/\"/g")
                    task_result_status=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_task_result', 'status')")
                    task_id=$(python -c "import yb_platform_util_py2; yb_platform_util_py2.get_key_value('$new_task_result', 'data')")
                fi
                echo $task_result
                echo $task_result_status
                echo $task_id
                if [[ ! $task_result_status == "success" ]]
                then
                    echo $task_id
                    exit
                fi

                if [[ ! -z "$skip_wait" ]]
                then
                    echo "Universe delete requested successfull. Use $task_id as task id to get status of universe."
                else
                    echo "Universe delete requested successfull. Use $task_id as task id to get status of universe."
                    TaskStatus ${task_id} ${customer_uuid} "Universe Deletion Successfull."
                fi
                ;;
            * ) echo "Aborted Delete universe";;
        esac
        ;;
    TASK_STATUS)
        if [[ -z "$task_id" ]]
        then
            echo "Task id required to get task status"
            printf" Use \`-t|--task <task_id>\` to pass task_id.\n"
        fi
        TaskStatus ${task_id} ${customer_uuid} "Task completed Successfull."
        ;;
    *)
        echo "Invalid opertaion"
        HelpMessage
        exit 255
        ;;
    
esac
