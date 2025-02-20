import argparse
import uinput
import signal
import subprocess

from threading import Thread
from time import sleep

parser = argparse.ArgumentParser(
    description="Turns the BTN_EXTRA of the mouse into a left click spammer. Requires sudo"
)
parser.add_argument(
    "device",
    help="The device path to listen for events, find the mouse path using `sudo libinput list-inputs`",
)
args = parser.parse_args()

extra_hold = b"EV_KEY / BTN_EXTRA                 1"
extra_release = b"EV_KEY / BTN_EXTRA                 0"


class ClickLoopThread(Thread):
    _kwargs = dict()  # so pyright stop complaining

    def __init__(self, device: uinput.Device):
        super().__init__()
        self.do_click = False
        self._loop = True
        self.device = device

    def run(self):
        while self._loop:
            if self.do_click:
                self.left_click()
            sleep(0.05)

    def left_click(self):
        # Need to sync both emits for uinput to consider a mouse click
        self.device.emit(uinput.BTN_LEFT, 1, syn=True)
        self.device.emit(uinput.BTN_LEFT, 0, syn=True)

    def stop_loop(self):
        self.do_click = False
        self._loop = False


def main():
    events = (
        uinput.BTN_LEFT,
        uinput.BTN_EXTRA,
    )

    with subprocess.Popen(
        ["sudo", "libinput", "record", args.device], stdout=subprocess.PIPE
    ) as input_proc:
        assert input_proc.stdout
        with uinput.Device(events) as device:
            click_thread = ClickLoopThread(device)
            click_thread.start()

            def _on_sigint(_, __):
                click_thread.stop_loop()

            signal.signal(signal.SIGINT, _on_sigint)

            while click_thread.is_alive():
                line = input_proc.stdout.readline()
                if extra_hold in line:
                    click_thread.do_click = True
                elif extra_release in line:
                    click_thread.do_click = False
