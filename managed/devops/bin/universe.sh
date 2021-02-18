# Copyright (c) YugaByte, Inc.
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt


# Save user current directory and move to shell script folder to run shell script


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
    base_url="$BASE_URL"
    auth_uuid="$AUTH"
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
            percentage_response=$(python3 -c "import python3_universe; python3_universe.get_task_details('$task_id', '$base_url', '$customer', '$auth_uuid',)")
            new_percentage_response=$(echo $percentage_response | sed "s/'/\"/g")
            percentage_status=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_percentage_response', 'status')")
            percentage=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_percentage_response', 'data')")
        else
            percentage_response=$(python -c "import python2_universe; python2_universe.get_task_details('$task_id', '$base_url', '$customer', '$auth_uuid',)")
            new_percentage_response=$(echo $percentage_response | sed "s/'/\"/g")
            percentage_status=$(python -c "import python2_universe; python2_universe.get_key_value('$new_percentage_response', 'status')")
            percentage=$(python -c "import python2_universe; python2_universe.get_key_value('$new_percentage_response', 'data')")
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
    echo "Use -h to get all Actions available."
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
# Example, sh universe.sh -c f33e3c9b-75ab-4c30-80ad-cba85646ea39 -n jd-script-8-2-21 -o GET
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
            echo "Use -h to get all command line argument"
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
    echo -e '\nScript to perform universe actions.'
    echo -e "\t1. Get existing Universe details by universe name."
    echo -e "\t2. Get existing Universe details by universe UUID."
    echo -e "\t3. Delete existing Universe by universe name."
    echo -e "\t4. Delete existing Universe by universe UUID."
    echo -e "\t5. Create Universe by universe Json."
    echo -e "\t6. Create Universe by universe Json with overwriting universe name."
    echo -e "\t7. Get progress of task. task can be create universe or delete universe."
    echo -e "\nSyntax: \n\t bash universe.sh <action> [params]"
    echo -e "\t Example, \n\t\tbash universe.sh get_universe -n test-universe"
    echo -e "\nActions:"
    echo -e "get_universe \n\t Get existing universe detail,"
    echo -e "\t Example, \n\t\tbash universe.sh get_universe -n test-universe\n"
    echo -e "add_universe |  create_universe\n\t Create Universe from json file,"
    echo -e "\t Example, \n\t\tbash universe.sh create_universe -f test-universe.json -n test-universe-by-name\n"
    echo -e "del_universe |  delete_universe\n\t Delete existing Universe,"
    echo -e "\t Example, \n\t\tbash universe.sh delete_universe -n test-universe\n"
    echo -e "task_status \n\t To get task status,"
    echo -e "\t Example, \n\t\tbash universe.sh task_status -t f33e3c9b-75ab-4c30-80ad-cba85646ea39\n"
    echo -e "\nParams:"
    echo -e "-c | --customer_uuid \n\t Pass customer UUID, Mandatory if have multiple customers present "
    echo -e "\t Example,\n\t\tbash universe.sh get_universe -c f8d2490a-f07c-4a40-a523-767f4f3b12da\n"
    echo -e "-u | --universe_uuid \n\t Pass Universe UUID,"
    echo -e "\t Example,\n\t\tbash universe.sh get_universe -u f8d2490a-f07c-4a40-a523-767f4f3b12da\n"
    echo -e "-n | --universe_name \n\t Universe name,"
    echo -e "\t Example,\n\t\tbash universe.sh get_universe -n test-universe\n"
    echo -e "-f | --file \n\t Input file for creating universe,"
    echo -e "\t Example, \n\t\tbash universe.sh create_universe -f test-universe.json -n test-universe-by-name\n"
    echo -e "-y | --yes \n\t Input yes for all confirmation prompts,"
    echo -e "\t Example, \n\t\tbash universe.sh delete_universe -n test-universe-by-name -y\n"
    echo -e "--no-wait \n\t To run command in background, not wait to complete task,"
    echo -e "\t Example, \n\t\tbash universe.sh create_universe -f test-universe.json --no-wait\n"
    echo -e "-t | --task \n\t Pass Task UUID to get task status,"
    echo -e "\t Example, \n\t\tbash universe.sh task_status -t f33e3c9b-75ab-4c30-80ad-cba85646ea39\n"
    echo -e "-h | --help \n\t Print help message, \n\t Example, \n\t\t bash universe.sh -h\n"
    echo
    exit 255
fi

# Validation to set Base_URL and AUTH UUID
if [[ -z "$BASE_URL" ]]
then
    echo -e "BASE_URL is not set. Set BASE_URL as env variable to proceed."
    echo -e "\n\texport BASE_URL=<base_url>\n\nExample,\n\texport BASE_URL=http://localhost:9000"
    exit 255
fi

if [[ -z "$AUTH" ]]
then
    echo -e "AUTH is not set. Set AUTH as env variable to proceed."
    echo -e "\n\texport AUTH=<auth_UUID>\n\nExample,\n\t export AUTH=e16d75cf-79af-4c57-8659-2f8c34223551"
    exit 255
fi

base_url="$BASE_URL"
auth_uuid="$AUTH"

if [[ -z "$customer_uuid" ]]
then
    if [[ $python_3 == "True" ]]
    then
        echo $auth_uuid
        customer_response=$(python3 -c "import python3_universe; python3_universe.get_customer_uuid('$base_url', '$auth_uuid')")
        echo $customer_response
        new_customer_response=$(echo $customer_response | sed "s/'/\"/g")
        customer_uuid_status=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_customer_response', 'status')")
        customer_uuid=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_customer_response', 'data')")
    else
        customer_response=$(python -c "import python2_universe; python2_universe.get_customer_uuid('$base_url', '$auth_uuid')")
        new_customer_response=$(echo $customer_response | sed "s/'/\"/g")
        customer_uuid_status=$(python -c "import python2_universe; python2_universe.get_key_value('$new_customer_response', 'status')")
        customer_uuid=$(python -c "import python2_universe; python2_universe.get_key_value('$new_customer_response', 'data')")
    fi

    if [[ ! $customer_uuid_status == "success" ]]
    then
        echo $customer_uuid
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
            echo -e "Input json file required. Use \`-i|--input <file_path>\` to pass json input file"
            exit 255
        fi

        if [[ $python_3 == "True" ]]
        then
            universe_reponse=$(python3 -c "import python3_universe; python3_universe.create_universe('$base_url', '$customer_uuid', '$auth_uuid', '$input_file', '$universe_name')")
            new_universe_reponse=$(echo $universe_reponse | sed "s/'/\"/g")
            universe_status=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_universe_reponse', 'status')")
            task_id=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_universe_reponse', 'data')")
        else 
            universe_reponse=$(python -c "import python2_universe; python2_universe.create_universe('$base_url', '$customer_uuid', '$auth_uuid', '$input_file', '$universe_name')")
            new_universe_reponse=$(echo $universe_reponse | sed "s/'/\"/g")
            universe_status=$(python -c "import python2_universe; python2_universe.get_key_value('$new_universe_reponse', 'status')")
            task_id=$(python -c "import python2_universe; python2_universe.get_key_value('$new_universe_reponse', 'data')")
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
                result=$(python3 -c "import python3_universe; python3_universe.get_universe_details_by_uuid('$base_url', '$customer_uuid', '$auth_uuid', '$universe_uuid', '$USER_DIR')")
                echo $result
            else 
                result=$(python3 -c "import python3_universe; python3_universe.get_universe_details_by_uuid('$base_url', '$customer_uuid', '$auth_uuid', '$universe_uuid', '$USER_DIR')")
                echo $result
            fi
        elif [[ ! -z "$universe_name" ]]
        then
            if [[ $python_3 == "True" ]]
            then
                result=$(python3 -c "import python3_universe; python3_universe.get_universe_details('$base_url', '$customer_uuid', '$auth_uuid', '$universe_name', '$USER_DIR')")
                echo $result
            else 
                result=$(python -c "import python2_universe; python2_universe.get_universe_details('$base_url', '$customer_uuid', '$auth_uuid', '$universe_name', '$USER_DIR')")
                echo $result
            fi
        else
            echo -e "Required universe name | uuid to get detail of universe."
            echo -e "Use \`-n|--universe_name <universe_name>\` to pass universe name."
            echo -e "Use \`-u|--universe_uuid <universe_uuid>\` to pass universe uuid."
            exit 255
        fi
        ;;
    DELETE_UNIVERSE|DEL_UNIVERSE)
        if [[ -z "$universe_uuid" ]] && [[ -z "$universe_name" ]]
        then
            echo -e "Required universe name | uuid to delete universe."
            echo -e "Use \`-n|--universe_name <universe_name>\` to pass universe name."
            echo -e "Use \`-u|--universe_uuid <universe_uuid>\` to pass universe uuid."
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
                        result=$(python3 -c "import python3_universe; python3_universe.get_universe_uuid('$base_url', '$customer_uuid', '$auth_uuid', '$universe_name')")
                        new_result=$(echo $result | sed "s/'/\"/g")
                        res=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_result', 'status')")
                        universe_uuid=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_result', 'data')")
                    else 
                        result=$(python -c "import python2_universe; python2_universe.get_universe_uuid('$base_url', '$customer_uuid', '$auth_uuid', '$universe_name')")
                        new_result=$(echo $result | sed "s/'/\"/g")
                        res=$(python -c "import python2_universe; python2_universe.get_key_value('$new_result', 'status')")
                        universe_uuid=$(python -c "import python2_universe; python2_universe.get_key_value('$new_result', 'data')")
                    fi
                    if [[ ! $res == "success" ]]
                    then
                        echo $universe_uuid
                        exit
                    fi
                fi

                if [[ $python_3 == "True" ]]
                then
                    task_result=$(python3 -c "import python3_universe; python3_universe.delete_universe_by_id('$base_url', '$customer_uuid', '$auth_uuid', '$universe_uuid')")
                    new_task_result=$(echo $task_result | sed "s/'/\"/g")
                    task_result_status=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_task_result', 'status')")
                    task_id=$(python3 -c "import python3_universe; python3_universe.get_key_value('$new_task_result', 'data')")
                else 
                    task_result=$(python -c "import python2_universe; python2_universe.delete_universe_by_id('$base_url', '$customer_uuid', '$auth_uuid', '$universe_uuid')")
                    new_task_result=$(echo $task_result | sed "s/'/\"/g")
                    task_result_status=$(python -c "import python2_universe; python2_universe.get_key_value('$new_task_result', 'status')")
                    task_id=$(python -c "import python2_universe; python2_universe.get_key_value('$new_task_result', 'data')")
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
            echo -e" Use \`-t|--task <task_id>\` to pass task_id."
        fi
        TaskStatus ${task_id} ${customer_uuid} "Task completed Successfull."
        ;;
    *)
        echo "Invalid opertaion"
        exit 255
        ;;
esac
