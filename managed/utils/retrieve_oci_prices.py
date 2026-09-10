#!/usr/bin/env python3
"""
Downloads OCI list pricing from Oracle's public products API and writes a trimmed
snapshot to managed/src/main/resources/oci_pricing. Should be run monthly or when
Oracle publishes major price / SKU changes.

Source: https://apexapps.oracle.com/pls/apex/cetools/api/v1/products/?currencyCode=USD
"""

import json
import logging
import os
import re
import shutil
from datetime import datetime, timedelta
from urllib.request import urlopen

PRODUCTS_URL = (
    "https://apexapps.oracle.com/pls/apex/cetools/api/v1/products/?currencyCode=USD"
)
YW_DIR = os.path.abspath(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
OCI_PRICE_DIR = os.path.join(YW_DIR, "src/main/resources/oci_pricing")
PRICELIST_FILE = os.path.join(OCI_PRICE_DIR, "pricelist.json")
VERSION_FILE = os.path.join(OCI_PRICE_DIR, "version_metadata.json")
UPDATE_INTERVAL = timedelta(weeks=4)
DATE_FORMAT = "%Y-%m-%d"

# displayName patterns mapping to family key. Order matters (more specific first).
# Dense I/O, GPU, Ax, VMware, Free, and Cloud@Customer are intentionally excluded.
COMPUTE_FAMILY_PATTERNS = [
    (re.compile(r"Compute - Optimized - X9", re.I), "OptimizedX9"),
    (re.compile(r"Compute - Standard - X9", re.I), "X9"),
    (re.compile(r"Compute - Standard - E6(?!.*Ax)", re.I), "E6"),
    (re.compile(r"Compute - Standard - E5", re.I), "E5"),
    (re.compile(r"Compute - Standard - E4", re.I), "E4"),
    (re.compile(r"Compute - Standard - E3", re.I), "E3"),
    (re.compile(r"Standard - E2 Micro", re.I), "E2Micro"),
    (re.compile(r"Compute - Standard - E2(?!.*Micro)", re.I), "E2"),
    (re.compile(r"Compute - Standard - A4(?!.*Ax)", re.I), "A4"),
    (re.compile(r"Compute - Standard - A2", re.I), "A2"),
    (re.compile(r"Compute - Standard - A1", re.I), "A1"),
    (re.compile(r"Virtual Machine Standard - X7", re.I), "X7"),
    (re.compile(r"Virtual Machine Standard - X5", re.I), "X5"),
    (re.compile(r"Virtual Machine Standard - B1", re.I), "B1"),
]

SKIP_NAME_PATTERNS = [
    re.compile(r"Dense\s*I/?O", re.I),
    re.compile(r"\bGPU\b", re.I),
    re.compile(r"\bAx\b", re.I),
    re.compile(r"VMware", re.I),
    re.compile(r"Free", re.I),
    re.compile(r"Cloud@Customer", re.I),
]


def get_usd_payg_price(item):
    """Return USD PAYG list price, preferring the paid tier over Always Free $0 bands."""
    for loc in item.get("currencyCodeLocalizations") or []:
        if loc.get("currencyCode") != "USD":
            continue
        payg = [p for p in (loc.get("prices") or []) if p.get("model") == "PAY_AS_YOU_GO"]
        if not payg:
            continue
        chosen = max(
            payg,
            key=lambda p: (float(p.get("rangeMin") or 0), float(p.get("value") or 0)),
        )
        return float(chosen.get("value", 0))
    return None


def should_skip_display_name(display_name):
    # E2 Micro is published only as an Always Free SKU; still ingest it.
    if re.search(r"E2.*Micro", display_name, re.I):
        return False
    return any(p.search(display_name) for p in SKIP_NAME_PATTERNS)


def match_compute_family(display_name):
    if should_skip_display_name(display_name):
        return None
    for pattern, family in COMPUTE_FAMILY_PATTERNS:
        if pattern.search(display_name):
            return family
    return None


def is_ocpu_meter(display_name, metric_name):
    return "OCPU" in (metric_name or "") or display_name.rstrip().endswith("OCPU")


def is_memory_meter(display_name, metric_name):
    metric = metric_name or ""
    return "Gigabyte" in metric or "Memory" in display_name


def parse_products(items):
    compute_families = {}
    block_volume = {}

    for item in items:
        category = item.get("serviceCategory") or ""
        display_name = item.get("displayName") or ""
        metric_name = item.get("metricName") or ""
        usd = get_usd_payg_price(item)
        if usd is None:
            continue

        if category == "Compute - Virtual Machine":
            family = match_compute_family(display_name)
            if not family:
                continue
            family_rates = compute_families.setdefault(
                family, {"ocpuPerHour": 0.0, "memoryGbPerHour": 0.0}
            )
            if is_ocpu_meter(display_name, metric_name):
                family_rates["ocpuPerHour"] = usd
            elif is_memory_meter(display_name, metric_name):
                family_rates["memoryGbPerHour"] = usd
            else:
                # Older shapes bill OCPU only (e.g. E2, X5, X7, B1).
                family_rates["ocpuPerHour"] = usd

        elif category == "Storage - Block Volumes":
            if "Free" in display_name:
                continue
            if display_name == "Storage - Block Volume - Storage":
                block_volume["storageGbPerMonth"] = usd
            elif display_name == "Storage - Block Volume - Performance Units":
                block_volume["vpuPerGbPerMonth"] = usd

    return compute_families, block_volume


def main():
    logging.info("Retrieving OCI pricing data...")

    if os.path.exists(VERSION_FILE):
        try:
            with open(VERSION_FILE) as f:
                old_version_data = json.load(f)
            old_date = old_version_data.get("date")
            if old_date and (
                datetime.now() - datetime.strptime(old_date, DATE_FORMAT)
                < UPDATE_INTERVAL
            ):
                logging.info("Pricing information is up to date - skipping download.")
                return
        except (IOError, ValueError) as e:
            logging.info("Will download new data: %s", e)

    with urlopen(PRODUCTS_URL) as url:
        payload = json.loads(url.read().decode())

    compute_families, block_volume = parse_products(payload.get("items") or [])
    if not compute_families:
        raise RuntimeError("No standard compute family prices found in OCI product list")
    if "storageGbPerMonth" not in block_volume or "vpuPerGbPerMonth" not in block_volume:
        raise RuntimeError("Missing OCI block volume meters in product list")

    logging.info(
        "Parsed %d compute families and block volume meters", len(compute_families)
    )

    shutil.rmtree(OCI_PRICE_DIR, ignore_errors=True)
    os.makedirs(OCI_PRICE_DIR)

    pricelist = {
        "lastUpdated": payload.get("lastUpdated"),
        "currency": "USD",
        "computeFamilies": dict(sorted(compute_families.items())),
        "blockVolume": block_volume,
    }
    with open(PRICELIST_FILE, "w+") as f:
        json.dump(pricelist, f, indent=2, sort_keys=False)
        f.write("\n")

    with open(VERSION_FILE, "w+") as f:
        json.dump(
            {
                "date": datetime.now().strftime(DATE_FORMAT),
                "sourceLastUpdated": payload.get("lastUpdated"),
            },
            f,
            indent=2,
        )
        f.write("\n")

    logging.info("Wrote %s", PRICELIST_FILE)
    logging.info("Finished retrieving OCI pricing data.")


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")
    main()
