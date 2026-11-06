import pathlib
import unittest


SOURCE = (pathlib.Path(__file__).parent / "net.c").read_text(encoding="utf-8")
COM_SOURCE = (pathlib.Path(__file__).parent / "com.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    start = SOURCE.index(name)
    start = SOURCE.index("{", start)
    depth = 0
    for end in range(start, len(SOURCE)):
        if SOURCE[end] == "{":
            depth += 1
        elif SOURCE[end] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start : end + 1]
    raise AssertionError(f"unterminated function {name}")


class NetSafetyTests(unittest.TestCase):
    def test_connection_thread_solely_owns_websocket_close(self):
        writer = function_body("writer_thread(void")
        connection = function_body("conn_thread(void")

        self.assertNotIn("close(fd)", writer)
        self.assertNotIn("pthread_detach(wt)", connection)
        self.assertIn("pthread_join(wt, NULL)", connection)
        self.assertLess(connection.index("socket_shutdown(fd)"), connection.index("pthread_join(wt, NULL)"))
        self.assertLess(connection.index("pthread_join(wt, NULL)"), connection.rindex("close(fd)"))

    def test_regular_files_are_checked_after_nofollow_open(self):
        opener = function_body("open_regular_nofollow(const char")

        for token in (
            "O_NOFOLLOW",
            "fstat(raw, &st)",
            "S_ISREG(st.st_mode)",
            "FILE_FLAG_OPEN_REPARSE_POINT",
            "GetFileInformationByHandle",
            "FILE_ATTRIBUTE_REPARSE_POINT",
        ):
            self.assertIn(token, opener)
        self.assertIn("open_regular_nofollow(fn,", function_body("serve_file(int"))

    def test_disconnect_releases_every_input_class(self):
        connection = function_body("conn_thread(void")
        self.assertIn("net_release_all_input()", connection)
        self.assertIn("released.type = 15", function_body("net_release_all_input(void"))
        self.assertIn(
            "e.type == 15) { memset(g_keys, 0, sizeof g_keys); "
            "memset(&g_pad, 0, sizeof g_pad); memset(&g_mouse, 0, sizeof g_mouse); }",
            COM_SOURCE,
        )

    def test_net_input_frames_have_one_unambiguous_size(self):
        connection = function_body("conn_thread(void")
        self.assertIn("len == sizeof(NetInput)", connection)
        self.assertNotIn("len >= sizeof(NetInput)", connection)


if __name__ == "__main__":
    unittest.main()
