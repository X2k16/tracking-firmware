# coding=utf-8

from client import Client
import logging
import queue
import traceback
import subprocess
import time


class QueuedClient(Client):
    def __init__(self, *args, **kwargs):
        self.queue = kwargs.pop("queue")
        super().__init__(*args, **kwargs)

    def on_felica(self, data):
        self.queue.put(data)

if __name__ == "__main__":
    FORMAT = '%(levelname)s %(asctime)s %(module)s %(message)s'
    logging.basicConfig(format=FORMAT, level=0)

    q = queue.Queue()
    client = QueuedClient(queue=q, daemon=False)

    index = 560
    prev_idm = None

    try:
        while True:
            try:
                data = q.get()
                if prev_idm == data["idm"]:
                    continue

                subprocess.Popen(["say" ,"-v" ,"Kyoko" ,"-r" ,"300", str(index)])

                print("{0},{1}".format(index, data["idm"]))

                index += 1
                prev_idm = data["idm"]

            except KeyboardInterrupt:
                raise
            except:
                traceback.print_exc()

    except KeyboardInterrupt:
        pass
