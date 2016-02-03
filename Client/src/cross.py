# coding=utf-8

import os
import logging
import queue
import requests
import datetime
import traceback
from client import Client

TOUCH_API_URL = os.environ.get("TOUCH_API_URL", "https://ticket.cross-party.com/tracking/internalapi/touches/")
TOUCH_API_KEY = os.environ.get("TOUCH_API_KEY", "CHANGE_ME")

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

    try:
        while True:
            try:
                data = q.get()
                response = requests.post(TOUCH_API_URL, json={
                    "date": (datetime.datetime.now()-datetime.timedelta(hours=7)).isoformat(),
                    "mac":data["macaddress"],
                    "card_id":data["idm"]
                }, headers={
                    "X-API-KEY": TOUCH_API_KEY
                })
                print(response.text)
                print(response.json())
            except KeyboardInterrupt:
                raise
            except:
                traceback.print_exc()

    except KeyboardInterrupt:
        pass
