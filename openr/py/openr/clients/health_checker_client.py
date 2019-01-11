#!/usr/bin/env python3

#
# Copyright (c) 2014-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from __future__ import absolute_import, division, print_function, unicode_literals

from builtins import object

import zmq
from openr.HealthChecker import ttypes as health_checker_types
from openr.utils import consts, zmq_socket


class HealthCheckerClient(object):
    def __init__(
        self,
        zmq_ctx,
        health_checker_cmd_port,
        timeout=consts.Consts.TIMEOUT_MS,
        proto_factory=consts.Consts.PROTO_FACTORY,
    ):
        self._health_checker_cmd_socket = zmq_socket.ZmqSocket(
            zmq_ctx, zmq.REQ, timeout, proto_factory
        )
        self._health_checker_cmd_socket.connect(health_checker_cmd_port)

    def peek(self):
        req_msg = health_checker_types.HealthCheckerRequest()
        req_msg.cmd = health_checker_types.HealthCheckerCmd.PEEK

        self._health_checker_cmd_socket.send_thrift_obj(req_msg)

        return self._health_checker_cmd_socket.recv_thrift_obj(
            health_checker_types.HealthCheckerPeekReply
        )
