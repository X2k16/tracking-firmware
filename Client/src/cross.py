# coding=utf-8

import os
import logging
import queue
import requests
import datetime
import traceback
import time
from client import Client

TOUCH_API_URL = os.environ.get("TOUCH_API_URL", "https://ticket.cross-party.com/tracking/internalapi")
TOUCH_API_KEY = os.environ.get("TOUCH_API_KEY", "CHANGE_ME")

CLIENT_ID = os.environ.get("CLIENT_ID", None)
if CLIENT_ID:
    CLIENT_ID = int(CLIENT_ID)

class QueuedClient(Client):
    def __init__(self, *args, **kwargs):
        self.queue = kwargs.pop("queue")
        super().__init__(*args, **kwargs)

    def on_felica(self, data):
        data["date"] = datetime.datetime.now()
        self.queue.put(data)

if __name__ == "__main__":
    FORMAT = '%(levelname)s %(asctime)s %(module)s %(message)s'
    logging.basicConfig(format=FORMAT, level=0)

    q = queue.Queue()
    client = QueuedClient(queue=q, daemon=False)

    try:
        while True:
            try:
                data = q.get(timeout=5.0)
                print(data)
                response = requests.post("{0}/touches/".format(TOUCH_API_URL), json={
                    "date": data["date"].isoformat()+"+00:00",
                    "mac":data["macaddress"],
                    "card_id":data["idm"],
                    "client": CLIENT_ID,
                }, headers={
                    "X-API-KEY": TOUCH_API_KEY
                })
                print(response.json())
            except KeyboardInterrupt:
                raise
            except queue.Empty:
                try:
                    if CLIENT_ID:
                        response = requests.put("{0}/clients/{1}".format(TOUCH_API_URL, CLIENT_ID), json={}, headers={
                            "X-API-KEY": TOUCH_API_KEY
                        })
                except:
                    traceback.print_exc()
                    time.sleep(1.0)
            except:
                traceback.print_exc()
                q.put(data)
                time.sleep(0.5)

    except KeyboardInterrupt:
        pass
