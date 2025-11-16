# Edited by Pawan Ambalkar - 9/23/2024
# Copyright 2018 The MLPerf Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# =============================================================================

from __future__ import annotations
from decimal import Decimal
from enum import Enum
from ipaddress import ip_address
from typing import Any, Callable, Optional, Dict, Tuple, List, Set, NoReturn, Union
import argparse
import atexit
import builtins
import configparser
import datetime
import logging
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid

from ptd_client_server.lib import common
from ptd_client_server.lib import summary as summarylib
from ptd_client_server.lib import time_sync

PTD_READ_ALL_COMMAND_AC_MULTICH = "RL,*,*"
PTD_READ_ALL_COMMAND_AC = "RL"
PTD_READ_ALL_COMMAND_DC = "DC-RL"

RE_PTD_LOG = re.compile(
    r"""^
        Time,  [^,]*,
        Watts, [^,]*,
        Volts, (?P<v> [^,]* ),
        Amps,  (?P<a> [^,]* ),
        PF, [^,]*,
        Mark,  (?P<mark> [^,]* )
    """,
    re.X,
)

ANALYZER_SLEEP_SECONDS: float = 10
_debug = os.getenv("MLPP_DEBUG") is not None

if _debug:
    ANALYZER_SLEEP_SECONDS = 0.5

# https://github.com/mlcommons/power-dev/issues/154#issuecomment-785188217
MULTICHANNEL_DEVICES = [48, 59, 61, 77]
# https://github.com/mlcommons/power-dev/issues/220#issue-835336923
DEVICE_TYPE_WT500 = 48

DC_DEVICES = [508, 549, 586]

MAX_RANGE_FOR_DEVICE = {
    8: 20,  # WT210
    49: 20,  # WT310
    52: 20,  # WT330
    77: 20,  # WT330_multichannel
    35: 40,  # WT500
    48: 40,  # WT500_multichannel
    47: 50,  # WT1800
    66: 30,  # WT5000
    508: 20,  # WT210 DC
    549: 20,  # WT310 DC
    586: 20,  # WT330 DC
}


class MeasurementEndedTooFastError(Exception):
    pass


class MaxVoltsAmpsNegativeValuesError(Exception):
    pass


class LitNotFoundError(Exception):
    pass


class ExtraChannelError(Exception):
    pass


# (PVA added MG) new function to merge power logs from multiple analyzers
def merge_power_logs(inputfiles: List[str], outputfile: str) -> None:
    # buffer contains a list for each analyzer
    input_data: List[List[List[str]]] = []  # List[analyzer_num][line_num][column_num]
    for analyzer_num in range(len(inputfiles)):
        with open(inputfiles[analyzer_num]) as f_in:
            input_data.append([])  # add a new buffer for this analyzer
            for line in f_in:
                input_data[analyzer_num].append(
                    list(line.rstrip().split(","))
                )  # add line to analyzer's buffer
    print(f"This is the length of input data: {len(input_data)}")
    with open(outputfile, "w") as f_out:
        # go line by line and combine
        for line_num in range(min([len(analyzer) for analyzer in input_data])):
            aggregate = [
                "Time",
                "None",
                "Watts",
                "None",
                "Volts",
                "None",
                "Amps",
                "None",
                "PF",
                "none",
                "Mark",
                "None",
            ]

            # (PVA added try except as sometimes the logs may have extra data point creating a mistach in index of analyzer 1)
            try:
                # borrowing time and mark from analyzer 1
                aggregate[1] = input_data[0][line_num][1]  # time
                aggregate[11] = input_data[0][line_num][11]  # mark

                combined_watts: float = 0.0
                for analyzer_num in range(len(input_data)):
                    # (MG) only add if the value is not -1 (i.e. if the analyzer was on)
                    if float(input_data[analyzer_num][line_num][3]) != -1:
                        # (MG) watts is absolute value in case the analyzer was mistakenly on the negative side
                        combined_watts += abs(
                            float(input_data[analyzer_num][line_num][3])
                        )
                # "Time", time, "Watts", watts1+watts2+...+watts(n), "Volts", volts, "Amps", amps, "PF", pf
                aggregate[3] = f"{combined_watts:.6f}"
                aggregate[5] = f"{-1:.6f}"  # volts
                aggregate[7] = f"{-1:.6f}"  # amps
                aggregate[9] = f"{-1:.6f}"  # pf
                f_out.write(",".join(aggregate))

                for analyzer_num in range(len(input_data)):
                    f_out.write(f",Analyzer{analyzer_num+1},")
                    f_out.write(",".join(input_data[analyzer_num][line_num]))
                f_out.write("\n")
            except:
                print(
                    "Mismatch of total number of lines between Analyzer logs, skipping line number {line_num} from the log."
                )


class Parser:
    def __init__(self, row: str):
        self.words = row.rstrip().split(",")
        self._cur_number = 0
        self.word_len = len(self.words)

    def skip(self) -> None:
        self.str()

    def lit(self, column_name: str) -> None:
        if self.words[self._cur_number] == column_name:
            self.skip()
            return
        raise LitNotFoundError(
            f"Expected {column_name!r}, got {self.words[self._cur_number]!r}"
        )

    def check(self, column_name: str) -> bool:
        return self.words[self._cur_number] == column_name

    def decimal(self) -> Any:
        return self._next(Decimal, "Decimal")

    def str(self) -> builtins.str:
        num, self._cur_number = self._cur_number, self._cur_number + 1
        return self.words[num]

    def _next(
        self, parse: Callable[[builtins.str], Any], expected_type: builtins.str
    ) -> Any:
        value = self.str()
        try:
            return parse(value)
        except Exception:
            logging.error(f"Expected {expected_type!r}, got {value!r}")
            raise

    def is_finished(self) -> bool:
        return self._cur_number >= len(self.words) - 1


def max_volts_amps_avg_watts(
    log_fname: str, mark: str, start_channel: int, amount_of_channels: int
) -> Tuple[str, str, str]:
    maxVolts = Decimal("-1")
    maxAmps = Decimal("-1")
    avgWatts = Decimal("-1")
    watts = []
    with open(log_fname, "r") as f:
        for line in f:
            m = RE_PTD_LOG.match(line.rstrip("\r\n"))
            if m and m["mark"] == mark:
                parser = Parser(line)
                parser.lit("Time")
                parser.skip()
                parser.lit("Watts")
                if amount_of_channels == 0:
                    watts_raw = parser.decimal()
                    if watts_raw > 0:
                        watts.append(watts_raw)
                else:
                    parser.skip()
                parser.lit("Volts")
                volts = parser.decimal()
                parser.lit("Amps")
                amps = parser.decimal()
                parser.lit("PF")
                parser.skip()
                parser.lit("Mark")
                parser.skip()
                maxVolts = max(maxVolts, volts)
                maxAmps = max(maxAmps, amps)
                channel_range = list(
                    range(start_channel, start_channel + amount_of_channels)
                )
                while not parser.is_finished():
                    is_sutable_channel = True
                    if not parser.check(f"Ch{channel_range[0]}"):
                        is_sutable_channel = False
                    else:
                        channel_range.pop(0)
                    parser.skip()
                    parser.lit("Watts")
                    watts_raw = parser.decimal()
                    parser.lit("Volts")
                    volts = parser.decimal()
                    parser.lit("Amps")
                    amps = parser.decimal()
                    parser.lit("PF")
                    parser.skip()
                    if is_sutable_channel:
                        maxVolts = max(maxVolts, volts)
                        maxAmps = max(maxAmps, amps)
                        if watts_raw > 0:
                            watts.append(watts_raw)
                    if len(channel_range) == 0:
                        break
                if len(channel_range):
                    raise ExtraChannelError(
                        f"There are extra ptd channels in configuration"
                    )
    if len(watts) >= 1:
        avgWatts = Decimal(sum(watts) / len(watts))
    else:
        avgWatts = Decimal(-1)
    return str(maxVolts), str(maxAmps), str("%.6f" % avgWatts)


def read_log(log_fname: str, mark: str) -> str:
    # TODO: The log file grows over time and never cleared.
    #       Probably, we need to fseek() here instead of reading from the start.
    result = []
    with open(log_fname, "r") as f:
        for line in f:
            m = RE_PTD_LOG.match(line.rstrip("\r\n"))
            if m and m["mark"] == mark:
                result.append(line)
    return "".join(result)


def exit_with_error_msg(error_msg: str) -> NoReturn:
    logging.fatal(error_msg)
    exit(1)


def tcp_port_is_occupied(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.1)
        return s.connect_ex(("127.0.0.1", port)) == 0


def get_host_port_from_listen_string(listen_str: str) -> Tuple[str, int]:
    try:
        host, port = listen_str.split(" ")
    except ValueError:
        raise ValueError(f"could not parse listen option {listen_str}")
    try:
        ip_address(host)
    except ValueError:
        raise ValueError(f"wrong listen option ip address {ip_address}")
    try:
        int_port = int(port)
    except ValueError:
        raise ValueError(f"could not parse listen option port {port} as integer")
    return (host, int_port)


class ServerConfig:
    def __init__(self, filename: str) -> None:
        conf = configparser.ConfigParser()
        try:
            conf.read_file(open(filename))
        except FileNotFoundError:
            exit_with_error_msg(f"Configuration file '{filename}' does not exist.")

        _UNSET = object()
        used: Dict[str, Set[str]] = {}

        def parse_channel(channel_value: str) -> List[int]:
            return [int(s) for s in channel_value.split(",")]

        def get(
            section: str,
            option: str,
            parse: Optional[Callable[[str], Any]] = None,
            fallback: Any = _UNSET,
        ) -> Any:
            used.setdefault(section.lower(), set()).add(option.lower())
            if fallback is _UNSET:
                try:
                    val = conf.get(section, option)
                except configparser.Error as e:
                    exit_with_error_msg(f"{filename}: config error: {e}")
            else:
                val = conf.get(section, option, fallback=fallback)

            if parse is not None and isinstance(val, str):
                try:
                    return parse(val)
                except Exception:
                    logging.exception(
                        f"{filename}: could not parse option {option!r} in {section!r}"
                    )
                    exit(1)
            else:
                return val

        self.tmp_dir = tempfile.TemporaryDirectory()

        self.ntp_server: str = get("server", "ntpServer")
        self.out_dir: str = self.tmp_dir.name
        self.host: str
        self.port: int
        self.host, self.port = get(
            "server",
            "listen",
            parse=get_host_port_from_listen_string,
            fallback=f"0.0.0.0 {common.DEFAULT_PORT}",
        )
        self.ranging_mode = get("server", "rangingMode", fallback="AUTO")

        self.analyzer_count: int = get("ptd", "analyzerCount", parse=int, fallback="1")

        # (PVA)Define the variables.
        self.ptd_channel: List[List[int]] = [[]] * self.analyzer_count
        self.ptd_port: List[int] = [0] * self.analyzer_count
        self.ptd_device_type: List[int] = [0] * self.analyzer_count
        self.ptd_interface_flag: List[str] = [""] * self.analyzer_count
        self.ptd_dc_flag: List[str] = [""] * self.analyzer_count
        self.ptd_device_port: List[str] = [""] * self.analyzer_count
        self.ptd_board_num: List[Optional[int]] = [None] * self.analyzer_count
        self.ptd_logfile: List[str] = [""] * self.analyzer_count
        self.ptd_command: List[List[str]] = [[]] * self.analyzer_count
        self.ptd_summary: List[Dict[str, Any]] = [{}] * self.analyzer_count

        for i in range(self.analyzer_count):
            self.ptd_channel[i] = get(
                f"analyzer{i+1}", "channel", parse=parse_channel, fallback=None
            )
            self.ptd_port[i] = get(
                f"analyzer{i+1}", "networkPort", parse=int, fallback="8888"
            )
            # TODO: validate ptd_device_type?
            self.ptd_device_type[i] = get(f"analyzer{i+1}", "deviceType", parse=int)
            # (PVA)DC flag is new so changed to other formats
            self.ptd_dc_flag[i] = get(f"analyzer{i+1}", "dcFlag", fallback=None)
            # TODO: validate ptd_interface_flag?
            self.ptd_interface_flag[i] = get(f"analyzer{i+1}", "interfaceFlag")
            self.ptd_device_port[i] = get(f"analyzer{i+1}", "devicePort")
            self.ptd_board_num[i] = get(
                f"analyzer{i+1}", "gpibBoard", parse=int, fallback=None
            )

            # (PVA) the self.tmp_dir.name is not changed to self.out_dir
            # we can have a list of supported/tested devices and throw a warning when new device is used?
            self.ptd_logfile[i] = os.path.join(
                self.tmp_dir.name, f"ptd_logfile_{i+1}.log"
            )

            self.ptd_command[i] = [
                get("ptd", "ptd"),
                "-l",
                self.ptd_logfile[i],
                "-p",
                str(self.ptd_port[i]),
                *(
                    []
                    if self.ptd_board_num[i] is None
                    else ["-b", str(self.ptd_board_num[i])]
                ),
                *(
                    []
                    if self.ptd_channel[i] is None
                    else ["-c", ",".join(str(x) for x in self.ptd_channel[i])]
                ),
                *([] if self.ptd_dc_flag[i] is None else [self.ptd_dc_flag[i]]),
                *(
                    []
                    if self.ptd_interface_flag[i] == ""
                    else [self.ptd_interface_flag[i]]
                ),
                str(self.ptd_device_type[i]),
                self.ptd_device_port[i],
            ]

            self.ptd_summary[i] = {
                "command": self.ptd_command[i],
                "device_type": self.ptd_device_type[i],
                "interface_flag": self.ptd_interface_flag[i],
                "dc_flag": self.ptd_dc_flag[i],
                "device_port": self.ptd_device_port[i],
                "channel": self.ptd_channel[i],
            }

        for section, used_items in used.items():
            unused_options = conf[section].keys() - set((i.lower() for i in used_items))
            if len(unused_options) != 0:
                logging.warning(
                    f"{filename}: ignoring unknown options in section {section!r}: "
                    f"{', '.join(unused_options)}"
                )

        unused_sections = (
            set(conf.sections())
            - {"server", "ptd"}
            - set([i for i in conf.sections() if i.startswith("analyzer")])
        )

        if len(unused_sections) != 0:
            logging.warning(
                f"{filename}: ignoring unknown sections: {', '.join(unused_sections)}"
            )

        # Check configuration
        self._check(filename)

    def _check(self, filename: str) -> None:
        for i in range(self.analyzer_count):
            if tcp_port_is_occupied(self.ptd_port[i]):
                exit_with_error_msg(
                    f"The PTDaemon port {self.ptd_port[i]} is already occupied."
                )

            if self.ptd_device_type[i] in MULTICHANNEL_DEVICES:
                if not self.ptd_channel[i]:
                    exit_with_error_msg(
                        f"{filename}: 'channel' value should be set for"
                        f" a multichannel device {self.ptd_device_type[i]}."
                    )
                if (
                    self.ptd_device_type[i] == DEVICE_TYPE_WT500
                    and len(self.ptd_channel[i]) != 1
                ):
                    exit_with_error_msg(
                        f"{filename}: 'channel' value should consist of one number"
                        f" for a multichannel device {self.ptd_device_type[i]} (Yokogawa WT500)."
                    )
                if (
                    self.ptd_device_type[i] != DEVICE_TYPE_WT500
                    and len(self.ptd_channel[i]) != 2
                ):
                    exit_with_error_msg(
                        f"{filename}: 'channel' value should consist of two numbers"
                        f" for a multichannel device {self.ptd_device_type[i]}."
                    )
            else:
                if self.ptd_channel[i] and len(self.ptd_channel[i]) != 1:
                    exit_with_error_msg(
                        f"{filename}: 'channel' value should consist of one number"
                        f" or be disabled for a 1-channel device {self.ptd_device_type[i]}."
                    )


class Ptd:
    def __init__(
        self, command: List[str], port: int, log_dir_path: str, analyzer: int
    ) -> None:
        self._process: Optional[subprocess.Popen[Any]] = None
        self._socket: Optional[socket.socket] = None
        self._proto: Optional[common.Proto] = None
        self._command = command
        self._analyzer = analyzer
        self._port = port
        self._init_Amps: Optional[str] = None
        self._init_Volts: Optional[str] = None
        atexit.register(self._force_terminate)
        self._tee: Optional[Tee] = None
        self._log_dir_path = log_dir_path
        self._messages = summarylib.PtdMessages()

    def start(self) -> None:
        try:
            self._start()
        except Exception:
            logging.exception("Could not start PTDaemon")
            exit(1)

    def _start(self) -> None:
        if self._process is not None:
            return
        if tcp_port_is_occupied(self._port):
            raise RuntimeError(
                f"Analyzer [{self._analyzer}] says the PTDaemon port {self._port} is already occupied"
            )
        logging.info(
            f"Analyzer [{self._analyzer}] is running PTDaemon: {self._command}. Make sure path to PTDaemon is correct in cfg file"
        )
        self._tee = Tee(
            os.path.join(self._log_dir_path, f"ptd_logs_analyzer_{self._analyzer}.txt")
        )
        env = os.environ
        env["TZ"] = "UTC"
        if sys.platform == "win32":
            # creationflags=subprocess.CREATE_NEW_PROCESS_GROUP:
            #   We do not want to pass ^C from the current console to the
            #   PTDaemon.  Instead, we terminate it explicitly in self.terminate().
            self._process = subprocess.Popen(
                self._command,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
                bufsize=1,
                universal_newlines=True,
                stdout=self._tee.w,
                stderr=subprocess.STDOUT,
                env=env,
            )
        else:
            self._process = subprocess.Popen(
                self._command,
                bufsize=1,
                universal_newlines=True,
                stdout=self._tee.w,
                stderr=subprocess.STDOUT,
                env=env,
            )
        self._tee.started()

        # Linux PTDaemon connected to WT333E over USB takes 17 seconds to fire
        # up.  We wait for 30 seconds to be sure.
        retries = 300

        s = None
        while s is None and retries > 0:
            if self._process is not None and self._process.poll() is not None:
                raise RuntimeError(
                    f"Analyzer [{self._analyzer}] says PTDaemon unexpectedly terminated"
                )
                # raise RuntimeError("PTDaemon unexpectedly terminated")
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                s.connect(("127.0.0.1", self._port))
            except ConnectionRefusedError:
                if common.sig.stopped:
                    exit()
                time.sleep(0.1)
                s = None
                retries -= 1
        if s is None:
            self.terminate()
            raise RuntimeError(
                f"Analyzer [{self._analyzer}] could not connect to PTDaemon"
            )
        self._socket = s
        self._proto = common.Proto(s)

        if self.cmd("Hello") != "Hello, PTDaemon here!":
            raise RuntimeError(f"Analyzer [{self._analyzer}] says this is not PTDaemon")

        self.cmd("Identify")  # reply traced in logs

        logging.info(f"Analyzer [{self._analyzer}] connected to PTDaemon")

        self._get_initial_range()

    def grab_power_data(self) -> Tuple[int, str, Optional[str], Optional[str]]:
        # (DM) Created method that will utilize SPEC's (only) preferred way of PTD usage and data gathering
        power_data_header = self.cmd(
            PTD_READ_ALL_COMMAND_AC_MULTICH
        )  # RL,*,* - command to show unread samples from sum channel and all channels individually
        if power_data_header is not None:
            if re.search("Invalid number of parameters", power_data_header):
                power_data_header = self.cmd(
                    PTD_READ_ALL_COMMAND_AC
                )  # RL - command to show unread samples in case of singlechannel AC
            elif power_data_header is not None and re.search(
                "Unknown command", power_data_header
            ):
                power_data_header = self.cmd(
                    PTD_READ_ALL_COMMAND_DC
                )  # DC-RL - command to show unread samples in case of DC meter
            if power_data_header is not None:
                number_of_samples = int(
                    power_data_header.split(" ")[1]
                )  # first line of response will have message: "Last XYZ samples".
        else:
            number_of_samples = 0
        grabbed_power_data = self.read(number_of_samples)
        if grabbed_power_data is None:
            exit_with_error_msg("Failed to get power data")
        grabbed_uncertainty_data = self.cmd("Uncertainty")
        grabbed_sanity_chk_data = self.cmd("Watts")
        return (
            number_of_samples,
            grabbed_power_data,
            grabbed_uncertainty_data,
            grabbed_sanity_chk_data,
        )

    def stop(self) -> None:
        self.cmd("Stop")

    def terminate(self) -> None:
        if self._proto is not None:
            self.cmd(f"SR,V,{self._init_Volts}")
            self.cmd(f"SR,A,{self._init_Amps}")
            logging.info(
                f"Analyzer [{self._analyzer}] set initial values for Amps {self._init_Amps} and Volts {self._init_Volts}"
            )
            self._proto = None

        if self._socket is not None:
            self._socket.close()
            self._socket = None

        if self._process is not None:
            logging.info(f"Analyzer [{self._analyzer}] stopping ptd...")
            self._process.terminate()
            try:
                self._process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
            self._process = None

        if self._tee is not None:
            self._tee.done()
            self._tee = None

    def _force_terminate(self) -> None:
        if self._process is not None:
            logging.info(f"Analyzer [{self._analyzer}] force stopping ptd...")
            self._process.kill()
            self._process.wait()
        self._process = None

        if self._tee is not None:
            self._tee.done()
            self._tee = None

    def cmd(self, cmd: str) -> Optional[str]:
        if self._proto is None:
            return None
        if self._process is None or self._process.poll() is not None:
            exit_with_error_msg(
                f"Analyzer [{self._analyzer}] says PTDaemon unexpectedly terminated"
            )
        logging.info(f"Analyzer [{self._analyzer}] is sending to ptd: {cmd!r}")
        self._proto.send(cmd)
        reply = self._proto.recv()
        if reply is None:
            exit_with_error_msg(
                f"Analyzer [{self._analyzer}] got no reply from PTDaemon :("
            )
        logging.info(f"Analyzer [{self._analyzer}] recieved reply from ptd: {reply!r}")
        self._messages.add(cmd, reply)
        return reply

    def read(self, number: int) -> Optional[str]:
        # (DM) had to add method that will unprovokedly read "number" of lines, so we can get all power data
        reply = ""
        if self._proto is None:
            return None
        if self._process is None or self._process.poll() is not None:
            exit_with_error_msg("PTDaemon unexpectedly terminated")
        logging.info(f"Analyzer [{self._analyzer}] is trying to read {number!r} lines")
        while number:
            rcvd = self._proto.recv()
            if rcvd is not None:
                reply += rcvd
                reply += "\n"
            else:
                exit_with_error_msg("Some samples can't be read")
            number -= 1
        if reply is None:
            exit_with_error_msg("Got no reply from PTDaemon")
        logging.info(f"Analyzer [{self._analyzer}] got reply from ptd: {reply!r}")
        return reply

    def _get_initial_range(self) -> None:
        # Normal Response: ?Ranges,{Amp Autorange},{Amp Range},{Volt Autorange},{Volt Range}\r\n?
        # Values: for autorange settings, -1 indicates ?unknown?, 0 = disabled, 1 = enabled
        # For range values, -1.0 indicates ?unknown?, >0 indicates actual value
        response = self.cmd("RR")
        if response is None or response == "":
            logging.error(f"Analyzer [{self._analyzer}] can't not get initial range")
            exit(1)

        response_list = response.split(",")

        def get_range_from_ranges_list(param_num: int, setting_name: str) -> str:
            try:
                if (
                    response_list[param_num] == "0"
                    and float(response_list[param_num + 1]) > 0
                ):
                    return response_list[param_num + 1]
            except (ValueError, IndexError):
                logging.warning(
                    f"Analyzer [{self._analyzer}] can not get ptd range value for {setting_name}"
                )
                return "Auto"
            return "Auto"

        self._init_Amps = get_range_from_ranges_list(1, "Amps")
        self._init_Volts = get_range_from_ranges_list(3, "Volts")
        logging.info(
            f"Analyzer [{self._analyzer}] says the initial range for Amps is {self._init_Amps} for Volts is {self._init_Volts}"
        )


class Server:
    def __init__(self, config: ServerConfig) -> None:
        self.session: Optional[Session] = None
        self._config = config
        self._stop = False
        self._summary: Optional[summarylib.Summary] = None
        self._last_session: Optional[str] = None
        self._last_session_dir_path: Optional[str] = None
        self._ptd: Optional[Ptd] = None

    def handle_connection(self, p: common.Proto) -> None:
        p.enable_keepalive()
        self._summary = summarylib.Summary()
        # self._summary.ptd_config = self._config.ptd_summary
        self._summary.ptd_config = [None] * self._config.analyzer_count
        for i in range(self._config.analyzer_count):
            self._summary.ptd_config[i] = self._config.ptd_summary[i]

        self._summary.debug = _debug
        self._last_session = self._last_session_dir_path = None

        common.log_redirect.start()
        with common.sig:
            # TODO: timeout and max msg size for recv
            magic = p.recv()
        self._summary.message((magic, time.time()), (common.MAGIC_SERVER, time.time()))
        p.send(common.MAGIC_SERVER)
        if magic != common.MAGIC_CLIENT:
            logging.error(
                f"Handshake failed, expected {common.MAGIC_CLIENT!r}, got {magic!r}"
            )
            return

        try:
            while True:
                with common.sig:
                    cmd, cmd_time = p.recv(), time.time()
                if cmd is None:
                    logging.info("Connection closed")
                    break
                logging.info(f"Got command from the client {cmd!r}")

                try:
                    reply = self._handle_cmd(cmd, p)
                except KeyboardInterrupt:
                    break
                except MeasurementEndedTooFastError as e:
                    logging.error(f"Got an exception: {e.args[0]}")
                    reply = f"Error: {e.args[0]}"
                except Exception:
                    logging.exception("Got an exception")
                    reply = "Error: exception"

                if reply is None:
                    continue

                if len(reply) < 1000:
                    logging.info(f"Sending reply to client {reply!r}")
                else:
                    logging.info(
                        f"Sending reply to client {reply[:50]!r}... len={len(reply)}"
                    )

                if self._summary is not None:
                    self._summary.message((cmd, cmd_time), (reply, time.time()))
                p.send(reply)
        finally:
            if self.session is not None:
                logging.warning("Client connection closed unexpectedly")
                self._drop_session()

            if self._stop:
                logging.info("Stopping the server")
                exit(0)

            self._last_session = self._last_session_dir_path = None

    def _handle_cmd(self, cmd: str, p: common.Proto) -> Optional[str]:
        cmd = cmd.split(",")
        if len(cmd) == 0:
            return "..."
        if cmd[0] == "time":
            return str(time.time())
        if cmd[0] == "set_ntp":
            time_sync.set_ntp(self._config.ntp_server)
            return "OK"
        if cmd[0] == "stop":
            logging.info("The server will be stopped after processing this client")
            self._stop = True
            return "OK"
        if cmd[0] == "new" and len(cmd) == 3:
            if self.session is not None:
                self.session.drop()
            if not common.check_label(cmd[1]):
                return "Error: invalid label"
            assert self._summary is not None
            self._summary.client_uuid = uuid.UUID(cmd[2])
            self._summary.server_uuid = uuid.uuid4()
            self.session = Session(self, cmd[1])
            self._summary.session_name = self.session._id
            self._last_session = self.session._id
            self._last_session_dir_path = self.session.log_dir_path
            return f"OK {self.session._id},{self._summary.server_uuid}"
        if cmd[0] == "session" and len(cmd) >= 3:
            if self.session is None or (self.session._id != cmd[1] and "*" != cmd[1]):
                return "Error: unknown session"
            cmd = cmd[2:]

            unbool = ["Error", "OK"]

            if cmd == ["start", "ranging"]:
                return unbool[int(self.session.start(Mode.RANGING))]
            elif cmd[0] == "start" and cmd[1] == "testing" and len(cmd) == 2:
                return unbool[int(self.session.start(Mode.TESTING))]
            elif cmd[0] == "start" and cmd[1] == "testing" and len(cmd) == 4:
                # TODO (PVA added MG) can't pass different values to different analyzers
                for i in range(self._config.analyzer_count):
                    for i in range(self._config.analyzer_count):
                        self.session._maxVolts[i] = cmd[2]
                        self.session._desirableCurrentRange[i] = cmd[3]
                        logging.info(
                            f"Analyzer [{i+1}] set initial values for Amps {cmd[3]} and Volts {cmd[2]}"
                        )
                # self.session._maxVolts = cmd[2]
                # self.session._desirableCurrentRange = cmd[3]
                r = self.session.start(Mode.TESTING)
                return unbool[int(r)] if type(r) == bool else str(r)

            if cmd == ["stop", "ranging"]:
                return unbool[int(self.session.stop(Mode.RANGING))]
            if cmd == ["stop", "testing"]:
                return unbool[int(self.session.stop(Mode.TESTING))]

            if cmd == ["done"]:
                self._drop_session()
                return "OK"

            return "Error Unknown session command"

        if (
            cmd[0] == "download"
            and cmd[1] == self._last_session
            and cmd[2] in common.FETCH_FILES_LIST
        ):
            assert self._last_session_dir_path is not None
            p.send_file(os.path.join(self._last_session_dir_path, cmd[2]))
            return None

        if cmd[0] == "cleanup" and cmd[1] == self._last_session:
            assert self._last_session_dir_path is not None
            shutil.rmtree(self._last_session_dir_path)
            return "OK"

        return "Error"

    def _drop_session(self) -> None:
        if self.session is None:
            common.log_redirect.stop()
            return

        power_logs = self.session.power_logs
        log_dir_path = self.session.log_dir_path
        # (PVA added MG) if more than one analyzer, merge ptd messages into one ptd_messages object
        ptd_messages = self.session._ptd[0]._messages
        if self._config.analyzer_count > 1:
            for i in range(1, self._config.analyzer_count):
                ptd_messages.merge(self.session._ptd[i]._messages)
        # ptd_messages = self.session._ptd._messages
        session, self.session = self.session, None
        summary, self._summary = self._summary, None

        try:
            session.drop()
        finally:
            common.log_redirect.stop(os.path.join(power_logs, "server.log"))

        if summary is not None:
            summary.ptd_messages = ptd_messages
            summary.hash_results(log_dir_path)
            summary.save(os.path.join(power_logs, "server.json"))

    def close(self) -> None:
        self._config.tmp_dir.cleanup()
        self._drop_session()


class SessionState(Enum):
    INITIAL = 0
    RANGING = 1
    RANGING_DONE = 2
    TESTING = 3
    TESTING_DONE = 4
    DONE = 5


class Mode(Enum):
    RANGING = 0
    TESTING = 1


class Tee:
    def __init__(self, fname: str) -> None:
        self._closed = False
        self._r, self.w = os.pipe()
        self._f = open(fname, "wb")
        self._thread = threading.Thread(target=self._run)
        self._thread.daemon = True
        self._thread.start()

    def started(self) -> None:
        """Should be called passing self.w to Popen()"""
        if not self._closed:
            os.close(self.w)
            self._closed = True

    def done(self) -> None:
        """Should be called after Popen.wait()"""
        if not self._closed:
            os.close(self.w)
            self._closed = True
        self._thread.join()

    def _run(self) -> None:
        try:
            while True:
                rd = os.read(self._r, 1024)
                if len(rd) == 0:
                    break
                rd_str = rd.decode(errors="ignore")
                sys.stderr.write(rd_str)
                sys.stderr.flush()
                self._f.write(rd)
        finally:
            self._f.close()
            os.close(self._r)


class Session:
    def __init__(self, server: Server, label: str) -> None:
        self._server: Server = server
        self._go_command_time: Optional[float] = None
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self._id: str = timestamp + "_" + label if label != "" else timestamp
        self.log_dir_path = os.path.join(self._server._config.out_dir, self._id)
        os.mkdir(self.log_dir_path)
        self.power_logs = os.path.join(self._server._config.out_dir, self._id, "power")
        os.mkdir(self.power_logs)

        self._ptd: List[Ptd] = [Ptd([""], 0, "", 0)] * server._config.analyzer_count

        for i in range(server._config.analyzer_count):
            self._ptd[i] = Ptd(
                server._config.ptd_command[i],
                server._config.ptd_port[i],
                self.power_logs,
                i + 1,
            )
        # State
        self._state = SessionState.INITIAL
        self._maxAmps: List[str] = ["0"] * server._config.analyzer_count
        self._maxVolts: List[str] = ["0"] * server._config.analyzer_count
        self._avgWatts: List[str] = ["0"] * server._config.analyzer_count
        self._desirableCurrentRange: List[str] = ["0"] * server._config.analyzer_count

    def start(self, mode: Mode) -> Union[bool, str]:
        if mode == Mode.RANGING and self._state == SessionState.RANGING:
            return True
        if mode == Mode.TESTING and self._state == SessionState.TESTING:
            return True

        assert self._server._summary is not None

        if mode == Mode.RANGING and self._state == SessionState.INITIAL:
            self._server._summary.phase("ranging", 0)
            for _ptd in self._ptd:
                _ptd.start()
                _ptd.cmd("SR,V,Auto")
            if self._server._config.ranging_mode == "AUTO":
                for _ptd in self._ptd:
                    _ptd.cmd("SR,A,Auto")
            elif self._server._config.ranging_mode == "MAX":
                for i in range(self._server._config.analyzer_count):
                    ptd_device_type = self._server._config.ptd_device_type[i]
                    if ptd_device_type in MAX_RANGE_FOR_DEVICE:
                        self._ptd[i].cmd(
                            f"SR,A,{MAX_RANGE_FOR_DEVICE[ptd_device_type]}"
                        )
                    else:
                        logging.warning(
                            f"Unknown max range type for device {ptd_device_type}, using AUTO"
                        )
                        self._ptd[i].cmd("SR,A,Auto")
            else:
                logging.warning("Unknown range mode, using AUTO")
            for _ptd in self._ptd:
                _ptd.cmd("SR,A,Auto")

            with common.sig:
                time.sleep(ANALYZER_SLEEP_SECONDS)
            logging.info("Starting ranging mode")
            threads = []
            # self._ptd.cmd(f"Go,1000,0,{self._id}_ranging")
            for _ptd in self._ptd:
                ptd_thread_start = threading.Thread(
                    target=_ptd.cmd, args=(f"Go,1000,0,{self._id}_ranging",)
                )
                threads.append(ptd_thread_start)
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()
            self._go_command_time = time.monotonic()

            self._state = SessionState.RANGING

            self._server._summary.phase("ranging", 1)
            return True

        if mode == Mode.TESTING and (
            (
                self._state == SessionState.INITIAL
                and self._maxVolts
                and self._desirableCurrentRange
            )
            or self._state == SessionState.RANGING_DONE
        ):
            self._server._summary.phase("testing", 0)
            for i in range(len(self._ptd)):
                self._ptd[i].start()

                r = self._ptd[i].cmd(f"SR,V,{self._maxVolts[i]}")
                if r and "Error" in r:
                    error = f"Error setting voltage range: {self._maxVolts}"
                    logging.error(error)
                    self.drop()
                    return error

                r = self._ptd[i].cmd(f"SR,A,{self._desirableCurrentRange[i]}")
                if r and "Error" in r:
                    error = (
                        f"Error setting current range: {self._desirableCurrentRange}"
                    )
                    logging.error(error)
                    self.drop()
                    return error

            with common.sig:
                time.sleep(ANALYZER_SLEEP_SECONDS)
            logging.info("Starting testing mode")
            for i in range(len(self._ptd)):
                logging.info(
                    f"Analyzer [{i+1}] maxAmps: {self._desirableCurrentRange[i]}, maxVolts: {self._maxVolts[i]}"
                )
            # self._ptd.cmd(f"Go,1000,0,{self._id}_testing")
            threads = []
            for _ptd in self._ptd:
                ptd_thread_start = threading.Thread(
                    target=_ptd.cmd, args=(f"Go,1000,0,{self._id}_testing",)
                )
                threads.append(ptd_thread_start)
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()

            self._state = SessionState.TESTING

            self._server._summary.phase("testing", 1)
            return True

        # Unexpected state
        return False

    def stop(self, mode: Mode) -> bool:
        if mode == Mode.RANGING and self._state == SessionState.RANGING_DONE:
            return True
        if mode == Mode.TESTING and self._state == SessionState.TESTING_DONE:
            return True

        assert self._server._summary is not None

        if mode == Mode.RANGING and self._state == SessionState.RANGING:
            self._server._summary.phase("ranging", 2)
        if mode == Mode.TESTING and self._state == SessionState.TESTING:
            self._server._summary.phase("testing", 2)

        with common.sig:
            time.sleep(ANALYZER_SLEEP_SECONDS)

        # TODO: handle exceptions?

        if mode == Mode.RANGING and self._state == SessionState.RANGING:
            self._state = SessionState.RANGING_DONE
            dirname = os.path.join(self.log_dir_path, "ranging")
            os.mkdir(dirname)
            for i in range(self._server._config.analyzer_count):
                self._ptd[i].stop()
                samples, log_data, uncertainty_data, sanity = self._ptd[
                    i
                ].grab_power_data()
                # (DM) really ugly function that will parse telnet log and reformat it in log that is same as ptd.log
                # If anyone knows how to do it better, please do
                lines = log_data.split("\n")
                formatted_log_data = ""
                for ii in range(len(lines)):
                    temp = lines[ii].split("Watts")
                    line_fixed = ""
                    for jj in range(len(temp)):
                        line_fixed += temp[jj]
                        if jj > 0 and (jj < len(temp) - 1 or len(temp) == 2):
                            if jj == 1:
                                if len(temp) == 2:
                                    line_fixed += ","
                                line_fixed += "Mark," + self._id + "_ranging"
                                if len(temp) > 2:
                                    line_fixed += ","
                            if len(temp) > 2:
                                line_fixed += "Ch" + str(jj) + ","
                        if jj < len(temp) - 1:
                            line_fixed += "Watts"
                    formatted_log_data += line_fixed + "\n"

                assert self._go_command_time is not None
                test_duration = time.monotonic() - self._go_command_time

                with open(os.path.join(dirname, f"spl_analyzer_{i+1}.csv"), "w") as f:
                    f.write(formatted_log_data)
                try:
                    start_channel = 0
                    channels_amount = 0

                    if self._server._config.ptd_channel[i] is not None:
                        if self._server._config.ptd_device_type[i] == DEVICE_TYPE_WT500:
                            start_channel = 1
                            channels_amount = self._server._config.ptd_channel[i][0]
                        else:
                            start_channel = self._server._config.ptd_channel[i][0]
                            if len(self._server._config.ptd_channel[i]) == 2:
                                channels_amount = self._server._config.ptd_channel[i][1]

                    (
                        self._maxVolts[i],
                        self._maxAmps[i],
                        self._avgWatts[i],
                    ) = max_volts_amps_avg_watts(
                        self._server._config.ptd_logfile[i],
                        self._id + "_ranging",
                        start_channel,
                        channels_amount,
                    )

                    # we will query average power consumed and depending on that, we will add fix to crest factor
                    # default is crest factor 3 (peak current is 3x rms current)
                    # PSUs under 75W don't have mandatory Power Factor Correction, so they can be arbitrarily dirty
                    # Tektronix' app note on power supplies claims that power supplies typically exhibit crest factor between 4 and 10
                    # https://assets.testequity.com/te1/Documents/pdf/power-measurements_AC-DC-an.pdf
                    # in order to achieve same peak detection, range should be 3.3 higher than max measured RMS (since crest factor of meter is 3 and 3*3.3 is almost 10 :) )

                    # DM:
                    # will have to deprecate this (forever), since it will cause increase in uncertainties (up to about 2.5x), so might easily blow over 1%.

                    # if float(self._avgWatts) < 75:
                    #     self._desirableCurrentRange = str(float(self._maxAmps) * 3.3)
                    # else:
                    #     self._desirableCurrentRange = str(float(self._maxAmps) * 1.1)

                    self._desirableCurrentRange[i] = str(float(self._maxAmps[i]) * 1.1)

                except MaxVoltsAmpsNegativeValuesError as e:
                    if test_duration < 1:
                        raise MeasurementEndedTooFastError(
                            f"the ranging measurement ended too fast (less than 1 second), no PTDaemon logs generated for {self._id!r}"
                        ) from e
                    else:
                        raise
            self._go_command_time = None
            self._server._summary.phase("ranging", 3)
            # (PVA - MG) merge the log files for each analyzer into one file
            input_files = [
                os.path.join(dirname, f"spl_analyzer_{i+1}.csv")
                for i in range(self._server._config.analyzer_count)
            ]
            output_file = os.path.join(dirname, "spl.txt")
            if self._server._config.analyzer_count > 1:
                merge_power_logs(input_files, output_file)
            else:
                shutil.copyfile(input_files[0], output_file)
            return True

        if mode == Mode.TESTING and self._state == SessionState.TESTING:
            self._state = SessionState.TESTING_DONE
            dirname = os.path.join(self.log_dir_path, "run_1")
            os.mkdir(dirname)

            for i in range(len(self._ptd)):
                watts = self._ptd[i].cmd("Watts")
                uncertainty = self._ptd[i].cmd("Uncertainty")
                self._ptd[i].stop()

                samples, log_data, uncertainty_data, sanity = self._ptd[
                    i
                ].grab_power_data()
                # (DM) TODO: figure out how to flag/report number of unvertain samples and how to disqualify bad run(s)lines = log_data.split("\n")
                lines = log_data.split("\n")
                formatted_log_data = ""
                for ii in range(len(lines)):
                    temp = lines[ii].split("Watts")
                    line_fixed = ""
                    for jj in range(len(temp)):
                        line_fixed += temp[jj]
                        if jj > 0 and (jj < len(temp) - 1 or len(temp) == 2):
                            if jj == 1:
                                if len(temp) == 2:
                                    line_fixed += ","
                                line_fixed += "Mark," + self._id + "_testing"
                                if len(temp) > 2:
                                    line_fixed += ","
                            if len(temp) > 2:
                                line_fixed += "Ch" + str(jj) + ","
                        if jj < len(temp) - 1:
                            line_fixed += "Watts"
                    formatted_log_data += line_fixed + "\n"
                with open(os.path.join(dirname, f"spl_analyzer_{i+1}.txt"), "w") as f:
                    f.write(formatted_log_data)
                with open(os.path.join(dirname, "ptd_out.txt"), "a+") as f:
                    f.write(f"Analyzer {i+1}:\n")
                    f.write(f"Power: {watts} \nUncertainty: {uncertainty}\n")
                self._server._summary.phase("testing", 3)
                # return True
            logging.info(
                f"Merging {self._server._config.analyzer_count} log files into one file"
            )
            logging.info(f"Merging spl log files into one file")
            input_files = [
                os.path.join(dirname, f"spl_analyzer_{i+1}.txt")
                for i in range(self._server._config.analyzer_count)
            ]
            output_file = os.path.join(dirname, "spl.txt")
            if self._server._config.analyzer_count > 1:
                merge_power_logs(input_files, output_file)
            else:
                shutil.copyfile(input_files[0], output_file)
            logging.info(f"Merging ptd log files into one file")
            for i in range(self._server._config.analyzer_count):
                with open(
                    os.path.join(
                        self._ptd[i]._log_dir_path, f"ptd_logs_analyzer_{i+1}.txt"
                    ),
                    "r",
                ) as f:
                    log_data = f.read()
                with open(
                    os.path.join(self._ptd[i]._log_dir_path, "ptd_logs.txt"), "a+"
                ) as f:
                    f.write("Analyzer " + str(i + 1) + "\n")
                    f.write(log_data)
            return True

        # Unexpected state
        return False

    def drop(self) -> None:
        for _ptd in self._ptd:
            _ptd.terminate()
        self._state = SessionState.DONE


def main() -> None:
    common.init("ptd-server")

    common.system_check()

    parser = argparse.ArgumentParser(description="Server for communication with PTD")
    required = parser.add_argument_group("required arguments")

    # fmt: off
    required.add_argument("-c", "--configurationFile", metavar="FILE", type=str, help="", required=True)
    # fmt: on
    args = parser.parse_args()

    config = ServerConfig(args.configurationFile)

    common.mkdir_if_ne(config.out_dir)

    if not time_sync.ntp_sync(config.ntp_server):
        exit_with_error_msg("Could not synchronize with NTP")

    common.log_sources()

    server = Server(config)
    try:
        server._ptd = Ptd(
            server._config.ptd_command[0],
            server._config.ptd_port[0],
            os.path.join(server._config.out_dir),
            1,
        )
        server._ptd.start()
        server._ptd.terminate()
        common.run_server(
            config.host,
            config.port,
            server.handle_connection,
        )
    except KeyboardInterrupt:
        pass
    finally:
        server.close()
