import queue
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import Mock

from dro_gui_tkinter_v16 import DROApp


class FakeSerial:
    def __init__(self):
        self.sent = []
        self.short = False
        self.error = False
        self.flush_count = 0

    def write(self, payload):
        if self.error:
            raise IOError("UART failure")
        self.sent.append(payload)
        return len(payload) - 1 if self.short else len(payload)

    def flush(self):
        self.flush_count += 1


class Mach3ControlTests(unittest.TestCase):
    def app(self):
        app = SimpleNamespace(
            mach3_active=False, serial_conn=FakeSerial(), serial_lock=threading.Lock(),
            pc_control_request=None, after=Mock(), last_manual_cmd={},
            pc_control_notice="", pc_control_notice_label=Mock(),
            pc_control_button=Mock(), auto_rx_queue=queue.Queue())
        app.write_uart = Mock(side_effect=lambda payload: DROApp.write_uart(app, payload))
        app.mach3_motion_blocked = lambda: DROApp.mach3_motion_blocked(app)
        app.pc_control_response_timeout = lambda req: DROApp.pc_control_response_timeout(app, req)
        return app

    def test_callback_uses_same_transmitter_as_jog_and_semi(self):
        app = self.app()
        DROApp.write_jog_command(app, {"x": 0, "y": 0, "z": 0})
        DROApp.write_semi_command(app, 100, 100)
        DROApp.toggle_pc_control(app)
        self.assertEqual(app.write_uart.call_count, 3)
        self.assertEqual(app.serial_conn.sent, [b"JOG,X:0.0,Y:0.0,Z:0.0\n",
                                              b"SEMI,X:100.0,Y:100.0\n", b"MACH3,ON\n"])

    def test_exact_on_off_bytes_and_ack(self):
        app = self.app()
        for payload, reply, active, status in (
            (b"MACH3,ON\n", "MACH3,ACK,ACTIVE:1", True, "CONTROLLO PC ATTIVO"),
            (b"MACH3,OFF\n", "MACH3,ACK,ACTIVE:0", False, "CONTROLLO PC DISATTIVATO"),
        ):
            DROApp.toggle_pc_control(app)
            self.assertEqual(app.serial_conn.sent[-1], payload)
            self.assertEqual(list(app.serial_conn.sent[-1]), list(payload))
            self.assertEqual(payload[-1], 10)
            app.pc_control_notice_label.configure.assert_called_with(text="CONTROLLO PC: comando inviato")
            DROApp.handle_serial_line(app, reply + "\r\n")
            DROApp.handle_mach3_line(app, app.auto_rx_queue.get_nowait())
            self.assertEqual(app.mach3_active, active)
            app.pc_control_notice_label.configure.assert_called_with(text=status)
        self.assertEqual(app.serial_conn.flush_count, 2)

    def test_short_write_and_exception_show_error_without_timer(self):
        for kind in ("short", "error"):
            with self.subTest(kind=kind):
                app = self.app()
                setattr(app.serial_conn, kind, True)
                DROApp.toggle_pc_control(app)
                app.pc_control_notice_label.configure.assert_called_once_with(
                    text="CONTROLLO PC: errore invio UART")
                self.assertFalse(app.mach3_active)
                app.after.assert_not_called()
                self.assertFalse(app.serial_lock.locked())

    def test_notice_only_after_complete_write_and_lock_is_held(self):
        app = self.app()
        original_write = app.serial_conn.write
        def write(payload):
            self.assertTrue(app.serial_lock.locked())
            self.assertNotEqual(app.pc_control_notice, "CONTROLLO PC: comando inviato")
            app.pc_control_notice_label.configure.assert_not_called()
            return original_write(payload)
        app.serial_conn.write = write
        DROApp.toggle_pc_control(app)
        self.assertEqual(app.pc_control_notice, "CONTROLLO PC: comando inviato")

    def test_errors_preserve_button_state(self):
        for code in ("LIMIT_ACTIVE", "AUTO_ACTIVE", "BUSY"):
            app = self.app()
            DROApp.toggle_pc_control(app)
            DROApp.handle_serial_line(app, f"MACH3,ERROR,CODE:{code}")
            DROApp.handle_mach3_line(app, app.auto_rx_queue.get_nowait())
            self.assertFalse(app.mach3_active)
            app.pc_control_button.configure.assert_not_called()
            app.pc_control_notice_label.configure.assert_called_with(
                text=f"CONTROLLO PC NON ATTIVATO: {code}")

    def test_timeout_and_late_ack(self):
        app = self.app()
        DROApp.toggle_pc_control(app)
        delay, callback = app.after.call_args.args
        self.assertEqual(delay, 1000)
        callback()
        self.assertEqual(app.pc_control_notice, "CONTROLLO PC: nessuna risposta dal Teensy")
        DROApp.handle_mach3_line(app, "MACH3,ACK,ACTIVE:1")
        self.assertEqual(app.pc_control_notice, "CONTROLLO PC ATTIVO")

    def test_queued_rx_cancels_timeout(self):
        app = self.app()
        DROApp.toggle_pc_control(app)
        callback = app.after.call_args.args[1]
        DROApp.handle_serial_line(app, "MACH3,ACK,ACTIVE:1")
        app.pc_control_notice_label.configure.reset_mock()
        callback()
        app.pc_control_notice_label.configure.assert_not_called()
