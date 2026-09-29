import requests, socket, os, json

class UnixSocketAdapter(requests.adapters.HTTPAdapter):
    def __init__(self, socket_path):
        super().__init__()
        self.socket_path = socket_path
    def get_connection(self, url, proxies=None):
        import urllib3
        class UnixSocketConnection(urllib3.connection.HTTPConnection):
            def __init__(self, socket_path, *args, **kwargs):
                super().__init__("localhost", *args, **kwargs)
                self.socket_path = socket_path
            def connect(self):
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(self.socket_path)
        class UnixSocketConnectionPool(urllib3.connectionpool.HTTPConnectionPool):
            def __init__(self, socket_path, *args, **kwargs):
                super().__init__("localhost", *args, **kwargs)
                self.socket_path = socket_path
            def _new_conn(self):
                return UnixSocketConnection(self.socket_path)
        return UnixSocketConnectionPool(self.socket_path)

# test script to test loading snapshot
