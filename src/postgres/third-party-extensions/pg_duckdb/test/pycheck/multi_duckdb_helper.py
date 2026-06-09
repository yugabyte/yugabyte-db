from contextlib import contextmanager
from queue import Queue
from threading import Thread

from .motherduck_token_helper import create_test_user
from .utils import make_new_duckdb_connection


class MDClientResponder:
    def __init__(self, res_queue):
        self.res_queue = res_queue

    def ok(self, result=None):
        self.res_queue.put({"success": True, "result": result})

    def ko(self, error):
        self.res_queue.put({"success": False, "error": error})


class MDClient:
    def __init__(self, spec):
        self.cmd_queue = Queue()
        self.res_queue = Queue()
        self.thread = Thread(
            target=MDClient.run,
            args=(
                self.cmd_queue,
                self.res_queue,
            ),
        )

        self.thread.start()

        spec = MDClient.normalize_spec(spec)
        self.send("createddb", [spec])

    @staticmethod
    def normalize_spec(spec):
        if isinstance(spec, str):
            return {"database": spec, "token": create_test_user()["token"]}
        elif isinstance(spec, dict):
            if "database" not in spec or "token" not in spec:
                raise ValueError("Invalid spec: missing 'database' or 'token'")
            return spec
        else:
            raise ValueError(f"Invalid spec: {spec}")

    @staticmethod
    @contextmanager
    def create(*args):
        clients = ()
        for arg in args:
            clients += (MDClient(arg),)

        all_clients = clients
        clients = clients[0] if len(clients) == 1 else clients

        try:
            yield clients
        finally:
            for client in all_clients:
                client.terminate()

    @staticmethod
    def run(cmd_queue, res_queue):
        responder = MDClientResponder(res_queue)
        token = None
        ddb_con = None
        while True:
            try:
                data = cmd_queue.get()
            except KeyboardInterrupt as e:
                responder.ko(e)
                continue  # Don't break the process, parent will

            cmd = data["command"]
            try:
                if cmd == "terminate":
                    responder.ok()
                    break
                elif cmd == "createddb":
                    spec = data["args"][0]
                    token = spec["token"]
                    db_name = spec["database"]
                    reset_db = spec.get("reset_db", True)
                    session_hint = spec.get("hint", None)
                    ddb_con = make_new_duckdb_connection(
                        db_name, token, reset_db, session_hint
                    )
                    responder.ok()
                elif cmd == "run_query":
                    r = ddb_con.execute(data["args"][0])
                    responder.ok(r.fetchall())
                elif cmd == "get_token":
                    responder.ok(token)
                else:
                    responder.ok(False)
            except Exception as e:
                responder.ko(e)

    def send(self, command, args=None):
        if not self.thread.is_alive():
            if command == "terminate":
                return
            raise RuntimeError(f"Process is not alive. Cannot send {command}.")

        self.cmd_queue.put({"command": command, "args": args})
        res = self.res_queue.get()
        if res["success"]:
            return res["result"]
        else:
            raise res["error"]

    def get_token(self):
        return self.send("get_token")

    def run_query(self, query):
        return self.send("run_query", [query])

    def terminate(self):
        self.send("terminate")
        self.thread.join()
