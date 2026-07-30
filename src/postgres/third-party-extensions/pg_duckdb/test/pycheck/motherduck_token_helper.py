import json
import os
from urllib.request import Request, urlopen

# Remove any normal user token from the environment so that we don't
# accidentally use it.
os.environ.pop("motherduck_token", None)

MD_TEST_USER_CREATOR_TOKEN = os.environ.get("md_test_user_creator_token")
MOTHERDUCK_TEST_TOKEN = os.environ.get("MOTHERDUCK_TEST_TOKEN")


def can_run_md_tests():
    return MD_TEST_USER_CREATOR_TOKEN is not None or MOTHERDUCK_TEST_TOKEN is not None


def can_run_md_multi_user_tests():
    return MD_TEST_USER_CREATOR_TOKEN is not None


def make_request(uri, headers, data_json):
    data = json.dumps(data_json).encode("utf-8")

    host = os.environ.get("motherduck_host", "api.motherduck.com")
    req = Request(url=f"https://{host}{uri}", data=data, headers=headers, method="POST")

    with urlopen(req) as f:
        res = f.read().decode("utf-8")
        return json.loads(res)


def create_read_scaling_token(user, token_name="test_rs"):
    return make_request(
        uri=f"/v1/users/{user['id']}/tokens",
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {user['token']}",
        },
        data_json={"name": token_name, "token_type": "read_scaling"},
    )


def create_test_user():
    """
    Given a `motherduck_token` environment variable, this function creates a test user and returns its details.
    The user to which the `motherduck_token` belongs needs to have the rights to create test users.

    For now, this function only supports one user, that will be re-used for all tests.
    """

    if MD_TEST_USER_CREATOR_TOKEN is None and MOTHERDUCK_TEST_TOKEN is not None:
        print(
            "Found `MOTHERDUCK_TEST_TOKEN` environment variable, using it as the test user token."
        )
        return {"token": MOTHERDUCK_TEST_TOKEN}

    token = MD_TEST_USER_CREATOR_TOKEN
    if not token:
        raise ValueError("You must provide a valid token to create a test user.")

    user = make_request(
        uri="/tuc/createTestUser",
        headers={"Content-Type": "application/json"},
        data_json={"token": token, "region": "aws-us-east-1"},
    )
    print(f"Created user with email='{user['testEmail']}' and id='{user['id']}'")
    return user
