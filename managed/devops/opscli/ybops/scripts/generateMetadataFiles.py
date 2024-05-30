import os
import json
import yaml

YB_LISCENCE = '''
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

'''

AWS_OS_VERSION = "8.9"
GCP_OS_VERSION = "8"
AZU_OS_VERSION = "8-gen2"

AWS_METADATA_FILENAME = "aws-metadata.yml"
GCP_METADATA_FILENAME = "gcp-metadata.yml"
AZURE_METADATA_FILENAME = "azu-metadata.yml"

AWS_AUTH_CMD = "aws sso login"
GCP_AUTH_CMD = "gcloud auth login"
AZU_AUTH_CMD = "az login"

AWS_ARM_CMD = 'aws ec2 describe-images \
                --region {region} \
                --owners aws-marketplace \
                --filters Name=architecture,Values=arm64 \
                          Name=description,Values="Official AlmaLinux OS {version} aarch64 image"'

AWS_X86_CMD = 'aws ec2 describe-images \
                --region {region} \
                --owners aws-marketplace \
                --filters Name=architecture,Values=x86_64 \
                          Name=description,Values="Official AlmaLinux OS {version} x86_64 image" '

GCP_X86_CMD = f'gcloud compute images \
                --project almalinux-cloud list \
                    --filter="architecture="X86_64" AND \
                              family="almalinux-{GCP_OS_VERSION}"" \
                    --format=json'

AZU_X86_CMD = f'az vm image list \
                --offer almalinux \
                --publisher almalinux \
                --sku {AZU_OS_VERSION} \
                --all'

# AZU_ARM_CMD = f'az vm image list \
#                 --offer almalinux \
#                 --publisher almalinux \
#                 --sku {AZU_OS_VERSION} \
#                 --all'


def get_aws_image_for_cmd(cmd, reg):
    try:
        result = os.popen(cmd.format(region=reg, version=AWS_OS_VERSION)).read()
        if 'AuthFailure' in result:
            print('Auth Error for region: ' + reg)
            return None
        result_json = json.loads(result)
        return result_json['Images'][0]
    except Exception as e:
        print('Exception while fetching image for region ' + reg)
        print(e)
        return None


def get_azu_image_for_cmd(cmd, arch):
    try:
        result = os.popen(cmd).read()
        if 'AuthFailure' in result:
            print('Auth Error')
            return None
        result_json = json.loads(result)
        filtered_result = [x for x in result_json if x['sku'] == AZU_OS_VERSION and (arch in x['offer'])]
        filtered_result.sort(key=lambda image: image['version'])
        latest_image = filtered_result[-1]
        return latest_image['urn']
    except Exception as e:
        print('Exception while fetching image')
        print(e)
        return None


def update_gcp_metadata_file(path):
    print('Starting GCP metadata file update')
    config = {}
    os.system(GCP_AUTH_CMD)
    with open(path) as config_file:
        config = yaml.load(config_file, yaml.SafeLoader)
    try:
        result = os.popen(GCP_X86_CMD).read()
        if result != '':
            print('Sucessfully fetched image')
            result_json = json.loads(result)
            image_json = result_json[0]
            image = image_json['name']
            for region in config['regions']:
                config['regions'][region]['image'] = image
            print('GCP Metadata file updated successfully')
        else:
            print('Failed to fetch image')
    except Exception as e:
        print('Failed to update GCP metadata file')
        print(e)
    finally:
        with open(path, "w") as file:
            file.write(YB_LISCENCE)
            file.write(yaml.dump(config, Dumper=yaml.SafeDumper))


def update_aws_metadata_file(path):
    print('Starting AWS metadata file update')
    config = {}
    os.system(AWS_AUTH_CMD)
    with open(path) as config_file:
        config = yaml.load(config_file, yaml.SafeLoader)
    try:
        for region in config['regions']:
            arm_image = get_aws_image_for_cmd(AWS_ARM_CMD, region)
            x86_image = get_aws_image_for_cmd(AWS_X86_CMD, region)
            if arm_image is not None:
                config['regions'][region]['arm_image'] = arm_image['ImageId']
            else:
                print('Failed to fetch Arm image for region ' + region)
            if x86_image is not None:
                config['regions'][region]['image'] = x86_image['ImageId']
            else:
                print('Failed to fetch x86 image for region ' + region)
            print('Image fetching completed for region ' + region)
        print('AWS metadata file updated successfully')
    except Exception as e:
        print('Failed to update GCP metadata file')
        print(e)
    finally:
        with open(path, "w") as file:
            file.write(YB_LISCENCE)
            file.write(yaml.dump(config, Dumper=yaml.SafeDumper))


def update_azure_metadata_file(path):
    print('Starting Azure metadata file update')
    config = {}
    os.system(AZU_AUTH_CMD)
    with open(path) as config_file:
        config = yaml.load(config_file, yaml.SafeLoader)
    try:
        x86_image = get_azu_image_for_cmd(AZU_X86_CMD, 'x86_64')
        if x86_image is None:
            print('Failed to fetch latest x86 image')
        # arm_image = get_azu_image_for_cmd(AZU_ARM_CMD)
        # if arm_image is None:
        #     print('Failed to get latest arm image')
        for region in config['regions']:
            # if arm_image is not None:
            #     config['regions'][region]['arm_image'] = arm_image
            if x86_image is not None:
                config['regions'][region]['image'] = x86_image
        print('Azure Metadata file updated successfully')
    except Exception as e:
        print('Failed to update azure metadata file')
        print(e)
    finally:
        with open(path, "w") as file:
            file.write(YB_LISCENCE)
            file.write(yaml.dump(config, Dumper=yaml.SafeDumper))


script_path = os.path.realpath(__file__)
metadata_folder_path = os.path.dirname(os.path.dirname(script_path)) + "/data/"
update_aws_metadata_file(metadata_folder_path + AWS_METADATA_FILENAME)
update_gcp_metadata_file(metadata_folder_path + GCP_METADATA_FILENAME)
update_azure_metadata_file(metadata_folder_path + AZURE_METADATA_FILENAME)
