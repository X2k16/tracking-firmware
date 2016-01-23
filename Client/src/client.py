# coding=utf-8

import logging
import serial
import os
import json
import threading
import subprocess
import requests

class Client(threading.Thread):

    def __init__(self, port=None, daemon=True):
        self.serial = None
        self.logger = logging.getLogger(__name__).getChild("Client")

        if not port:
            for f in  os.listdir('/dev'):
                if f.startswith("tty.usbserial") or f.startswith("ttyUSB"):
                    port = "/dev/{0}".format(f)
                    break
            else:
                raise ValueError("Cannot find port")

        self.logger.info("Open %s", port)
        self.serial = serial.Serial(port, 115200)

        super().__init__()
        self.daemon = daemon
        self.start()

    def __del__(self):
        if self.serial:
            self.logger.info("Close serial port")
            self.serial.close()

    def run(self):
        self.logger.info("Start read thread")
        while True:
            line = self.serial.readline().strip().decode("ascii")
            if not line:
                continue
            self.logger.debug("Receive new line '%s'", line)

            if line.startswith("{"):
                # JSON
                try:
                    data = json.loads(line)
                except:
                    pass
                else:
                    self._on_message(data)

    def _on_message(self, data):

        message_type = data.get("type")
        if not message_type:
            return

        if message_type == "debug":
            "debug message"
        elif message_type == "felica":
            self.on_felica(data)

    def on_felica(self, data):
        pass
