#!/usr/bin/env python3
"""
Downloads all relevant AWS price info to managed/src/main/resources/aws_prices. Should be run
monthly or with major AWS pricing changes (e.g. API updates or adding regions).

If running on MacOs and getting certificate error - try this: https://stackoverflow.com/a/49953629
"""
import json
import logging
import os
import shutil
import tarfile
from datetime import datetime, timedelta
from urllib.request import urlopen

BASE_PRICING_URL = "https://pricing.us-east-1.amazonaws.com"
REGION_INDEX_URL = BASE_PRICING_URL + "/offers/v1.0/aws/AmazonEC2/current/region_index.json"
SUPPORTED_STORAGE_TYPES = ["io1", "gp2", "gp3"]
TARGET_DIRECTORY = os.path.expanduser('')
YW_DIR = os.path.abspath(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
AWS_PRICE_DIR = os.path.join(YW_DIR, "src/main/resources/aws_pricing")
VERSION_FILE = os.path.join(AWS_PRICE_DIR, "version_metadata.json")
UPDATE_INTERVAL = timedelta(weeks=4)
DATE_FORMAT = "%Y-%m-%d"


def is_provisioned_throughput(product_pair):
    product = product_pair[1]
    att = product['attributes']
    return product.get('productFamily') == "Provisioned Throughput"


def is_system_operation(product_pair):
    product = product_pair[1]
    att = product['attributes']
    return product.get('productFamily') == "System Operation"


def is_compute_instance(product_pair):
    product = product_pair[1]
    return product.get('productFamily') == "Compute Instance"


def is_storage(product_pair):
    product = product_pair[1]
    att = product['attributes']
    return product.get('productFamily') == "Storage"


def should_save(product_pair):
    product = product_pair[1]
    att = product['attributes']
    isSupportedInstance = att.get('preInstalledSw', 'NA') == 'NA'\
        and 'BoxUsage' in att.get('usagetype')
    return (
        (is_compute_instance(product_pair)
            and att.get('servicecode') == "AmazonEC2"
            and att.get('operatingSystem') == "Linux"
            and att.get('licenseModel') in ("No License required", "NA")
            and ("SSD" in att.get('storage') or "EBS" in att.get('storage'))
            and att.get('currentGeneration') == "Yes"
            and att.get('tenancy') == "Shared"
            and isSupportedInstance)
        or (is_storage(product_pair)
            and att.get('volumeApiName') in SUPPORTED_STORAGE_TYPES)
        or (is_system_operation(product_pair)
            and att.get('group') == "EBS IOPS"
            and att.get('volumeApiName') in SUPPORTED_STORAGE_TYPES)
        or (is_provisioned_throughput(product_pair)
            and att.get('group') == "EBS Throughput"
            and att.get('volumeApiName') in SUPPORTED_STORAGE_TYPES))


def create_pricing_file(region, region_data):
    products = region_data['products']

    kept_products = dict(filter(should_save, products.items()))
    kept_compute_instances = len(dict(filter(is_compute_instance, kept_products.items())))
    kept_storages = len(dict(filter(is_storage, kept_products.items())))

    logging.info("Parsed %d compute instances and %d storages",
                 kept_compute_instances, kept_storages)

    should_save_pricing = kept_compute_instances > 0 and kept_storages > 0

    if not should_save_pricing:
        logging.info("Skipping region %s", region)
        return

    od = region_data['terms']['OnDemand']
    kept_od = {key: od[key] for key in kept_products}

    final_data = {'terms': {'OnDemand': kept_od}, 'products': kept_products}
    return final_data


def main():
    logging.info("Retrieving AWS pricing data...")
    with urlopen(REGION_INDEX_URL) as url:
        region_metadata = json.loads(url.read().decode()).get('regions')

    # Only update pricing data if it is old or if it doesn't exist.
    if os.path.exists(VERSION_FILE):
        old_version_data = {}
        try:
            with open(VERSION_FILE) as f:
                old_version_data = json.load(f)
        except IOError as e:
            logging.info("Failed to read %s and will download new data: %s", VERSION_FILE, e)
        old_date = old_version_data.get("date")
        if old_date and (
                datetime.now() - datetime.strptime(old_date, DATE_FORMAT) < UPDATE_INTERVAL):
            logging.info("Pricing information is up to date - skipping download.")
            return

    logging.info("Removing old pricing data if exists.")
    shutil.rmtree(AWS_PRICE_DIR, ignore_errors=True)

    os.makedirs(AWS_PRICE_DIR)

    for region in region_metadata:
        region_price_url = BASE_PRICING_URL + region_metadata[region]['currentVersionUrl']
        logging.info("Downloading information for %s from %s", region, region_price_url)
        region_data = {}
        with urlopen(region_price_url) as url:
            region_data = json.loads(url.read().decode())
        final_data = create_pricing_file(region, region_data)

        if not final_data:
            continue

        target_file = os.path.join(AWS_PRICE_DIR, region)
        with open(target_file, 'w+') as f:
            json.dump(final_data, f, indent=4)
        logging.info("Parsed %s info to %s", region, target_file)

        target_tar = target_file + '.tar.gz'
        with tarfile.open(target_tar, "w:gz") as target_tar_file:
            target_tar_file.add(target_file, arcname=region)
            target_tar_file.close()
        logging.info("Archived %s info to %s", target_file, target_tar_file)

        os.remove(target_file)
        logging.info("Removed %s", target_file)

    with open(VERSION_FILE, 'w+') as f:
        startDate = datetime.now().strftime(DATE_FORMAT)
        json.dump({"date": startDate}, f, indent=4)

    logging.info("Finished retrieving AWS pricing data.")


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")
    main()
