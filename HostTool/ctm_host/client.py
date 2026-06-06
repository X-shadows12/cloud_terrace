from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
import time

from .protocol import (
    CONFIG_BY_INDEX,
    CONFIG_BY_KEY,
    Command,
    ConfigItem,
    ControlMode,
    CanMessage,
    ctm_crc32,
    decode_status,
    make_can_id,
    pack_config_value,
    pack_float,
    pack_i32,
    pack_u32,
    parse_can_id,
    unpack_config_value,
    unpack_float,
    unpack_i32,
)


APP_MAX_SIZE = 0x40000
APP_BASE_ADDR = 0x08000000


@dataclass(frozen=True)
class StatusWord:
    status_code: int
    errors_code: int
    status: list[str]
    errors: list[str]


@dataclass(frozen=True)
class ProgressEvent:
    kind: str
    step: int
    value: int | float | bytes


class CtmError(RuntimeError):
    pass


class CtmClient:
    def __init__(self, bus, node_id: int = 1, response_timeout_ms: int = 400) -> None:
        self.bus = bus
        self.node_id = int(node_id)
        self.base_node_id = int(node_id)
        self.target_axis = "left"
        self.response_timeout_ms = int(response_timeout_ms)
        self.on_event: Callable[[ProgressEvent], None] | None = None

    def set_node_id(self, node_id: int) -> None:
        self.set_base_node_id(node_id)

    def set_base_node_id(self, node_id: int) -> None:
        base_node_id = int(node_id)
        if not 1 <= base_node_id <= 30:
            raise CtmError("base node id must be in range 1..30 for dual-axis firmware")
        self.base_node_id = base_node_id
        self.node_id = base_node_id

    def set_target_axis(self, target_axis: str) -> None:
        target = target_axis.strip().lower()
        aliases = {
            "left": "left",
            "l": "left",
            "axis0": "left",
            "axis_0": "left",
            "right": "right",
            "r": "right",
            "axis1": "right",
            "axis_1": "right",
            "broadcast": "broadcast",
            "all": "broadcast",
            "0": "broadcast",
        }
        if target not in aliases:
            raise CtmError(f"unknown target axis: {target_axis}")
        self.target_axis = aliases[target]

    def get_target_axis(self) -> str:
        return self.target_axis

    def call_axis(self, target_axis: str, method_name: str, *args):
        return self._with_target_axis(target_axis, lambda: getattr(self, method_name)(*args))

    def set_op_mode(self, mode: ControlMode | int) -> None:
        self._send_ack(Command.SET_OP_MODE, bytes([int(mode) & 0xFF]))

    def enable(self) -> None:
        self._send_ack(Command.MOTOR_ENABLE)

    def disable(self) -> None:
        self._send_ack(Command.MOTOR_DISABLE)

    def set_torque(self, current_a: float) -> None:
        self._send_no_reply(Command.SET_TORQUE, pack_float(current_a))

    def set_velocity(self, velocity_rps: float) -> None:
        self._send_no_reply(Command.SET_VELOCITY, pack_float(velocity_rps))

    def set_position(self, position_rev: float) -> None:
        self._send_no_reply(Command.SET_POSITION, pack_float(position_rev))

    def sync(self) -> None:
        self._send_no_reply(Command.SYNC)

    def broadcast_sync(self) -> None:
        self._with_target_axis("broadcast", self.sync)

    def set_sync_target_enable(self, enabled: bool) -> None:
        self.set_config(CONFIG_BY_KEY["sync_target_enable"], 1 if enabled else 0)

    def set_axes_sync_target_enable(self, enabled: bool) -> None:
        for axis in ("left", "right"):
            self.call_axis(axis, "set_sync_target_enable", enabled)

    def set_home(self) -> None:
        self._send_ack(Command.SET_HOME)

    def reset_error(self) -> None:
        self._send_ack(Command.ERROR_RESET)

    def get_statusword(self) -> StatusWord:
        self._reject_broadcast_read("get_statusword")
        reply = self._request(Command.GET_STATUSWORD)
        if len(reply.data) < 2:
            raise CtmError("状态字响应长度不足")
        status, errors = decode_status(reply.data[0], reply.data[1])
        return StatusWord(reply.data[0], reply.data[1], status, errors)

    def get_axis_statusword(self, target_axis: str) -> StatusWord:
        return self._with_target_axis(target_axis, self.get_statusword)

    def get_value(self, index: int) -> float:
        self._reject_broadcast_read("get_value")
        reply = self._request(Command.GET_VALUE_1, bytes([index & 0xFF]))
        return unpack_float(reply.data)

    def get_axis_value(self, target_axis: str, index: int) -> float:
        return self._with_target_axis(target_axis, lambda: self.get_value(index))

    def get_config(self, item: ConfigItem) -> int | float:
        self._reject_broadcast_read("get_config")
        request = pack_i32(item.index) + b"\x00\x00\x00\x00"
        reply = self._request(Command.GET_CONFIG, request)
        echoed_index = unpack_i32(reply.data[:4])
        if echoed_index == -1:
            raise CtmError(f"参数序号 {item.index} 被固件拒绝")
        return unpack_config_value(item, reply.data[4:8])

    def set_config(self, item: ConfigItem, value: str | int | float) -> None:
        self._reject_broadcast_read("set_config")
        payload = pack_i32(item.index) + pack_config_value(item, value)
        reply = self._request(Command.SET_CONFIG, payload)
        echoed_index = unpack_i32(reply.data[:4])
        if echoed_index == -1:
            raise CtmError(f"参数序号 {item.index} 被固件拒绝")

    def save_all_config(self) -> None:
        self._send_ack(Command.SAVE_ALL_CONFIG, timeout_ms=2500)

    def reset_all_config(self) -> None:
        self._send_ack(Command.RESET_ALL_CONFIG, timeout_ms=1000)

    def get_fw_version(self) -> tuple[int, int]:
        self._reject_broadcast_read("get_fw_version")
        reply = self._request(Command.GET_FW_VERSION)
        if len(reply.data) < 2:
            raise CtmError("固件版本响应长度不足")
        return reply.data[0], reply.data[1]

    def get_axis_fw_version(self, target_axis: str) -> tuple[int, int]:
        return self._with_target_axis(target_axis, self.get_fw_version)

    def start_calibration(self) -> None:
        self._send_ack(Command.CALIB_START, timeout_ms=1000)

    def abort_calibration(self) -> None:
        self._send_ack(Command.CALIB_ABORT, timeout_ms=1000)

    def start_anticogging(self) -> None:
        self._send_ack(Command.ANTICOGGING_START, timeout_ms=1000)

    def abort_anticogging(self) -> None:
        self._send_ack(Command.ANTICOGGING_ABORT, timeout_ms=1000)

    def poll_events(self, limit: int = 40) -> list[ProgressEvent]:
        events: list[ProgressEvent] = []
        for _ in range(limit):
            msg = self.bus.recv(timeout_ms=0)
            if msg is None:
                break
            event = self._message_to_event(msg)
            if event is not None:
                events.append(event)
                if self.on_event is not None:
                    self.on_event(event)
        return events

    def dfu_update(self, path: str | Path, progress: Callable[[int, int], None] | None = None) -> None:
        self._reject_broadcast_read("dfu_update")
        data = load_firmware_image(path)
        if len(data) > APP_MAX_SIZE:
            raise CtmError(f"固件文件超过 {APP_MAX_SIZE} 字节")
        if len(data) % 4:
            data += b"\xFF" * (4 - len(data) % 4)

        crc = ctm_crc32(data)
        total = len(data)

        self._send_ack(Command.DFU_START, timeout_ms=8000)
        for offset in range(0, total, 8):
            chunk = data[offset : offset + 8]
            if len(chunk) % 4:
                chunk += b"\xFF" * (4 - len(chunk) % 4)
            self._send_ack(Command.DFU_DATA, chunk, timeout_ms=1200)
            if progress is not None:
                progress(min(offset + len(chunk), total), total)

        self._send_ack(Command.DFU_END, pack_u32(total) + pack_u32(crc), timeout_ms=5000)

    def _send_no_reply(self, command: Command, data: bytes = b"") -> None:
        node_id = self._target_node_id()
        self.bus.send(CanMessage(make_can_id(node_id, command), data[:8]))

    def _send_ack(self, command: Command, data: bytes = b"", timeout_ms: int | None = None) -> None:
        reply = self._request(command, data, timeout_ms)
        if not reply.data or reply.data[0] != 0:
            code = reply.data[0] if reply.data else None
            raise CtmError(f"{command.name} 执行失败，ack={code!r}")

    def _request(self, command: Command, data: bytes = b"", timeout_ms: int | None = None) -> CanMessage:
        node_id = self._target_node_id()
        self.bus.send(CanMessage(make_can_id(node_id, command), data[:8]))
        return self._wait_for(command, timeout_ms or self.response_timeout_ms, node_id)

    def _wait_for(self, command: Command, timeout_ms: int, request_node_id: int) -> CanMessage:
        deadline = time.monotonic() + timeout_ms / 1000.0
        expected_nodes = self._expected_reply_nodes(request_node_id)
        while time.monotonic() < deadline:
            msg = self.bus.recv(timeout_ms=10)
            if msg is None:
                continue
            parsed = parse_can_id(msg.can_id)
            if not parsed.echo:
                continue
            if parsed.node_id not in expected_nodes:
                continue
            if parsed.command == int(command):
                return msg
            event = self._message_to_event(msg)
            if event is not None and self.on_event is not None:
                self.on_event(event)
        raise CtmError(f"等待 {command.name} 响应超时")

    def _message_to_event(self, msg: CanMessage) -> ProgressEvent | None:
        parsed = parse_can_id(msg.can_id)
        if not parsed.echo or parsed.node_id not in self._selected_reply_nodes():
            return None

        if parsed.command == Command.CALIB_REPORT and len(msg.data) >= 8:
            return ProgressEvent("calibration", unpack_i32(msg.data[:4]), msg.data[4:8])

        if parsed.command == Command.ANTICOGGING_REPORT and len(msg.data) >= 8:
            return ProgressEvent("anticogging", unpack_i32(msg.data[:4]), unpack_i32(msg.data[4:8]))

        if parsed.command == Command.STATUSWORD_REPORT and len(msg.data) >= 2:
            status, errors = decode_status(msg.data[0], msg.data[1])
            return ProgressEvent("statusword", msg.data[0], ",".join(status + errors))

        return None

    def _target_node_id(self) -> int:
        if self.target_axis == "broadcast":
            return 0
        if self.target_axis == "right":
            return self.base_node_id + 1
        return self.base_node_id

    def _expected_reply_nodes(self, request_node_id: int) -> set[int]:
        if request_node_id == 0:
            return {self.base_node_id, self.base_node_id + 1}
        return {request_node_id}

    def _selected_reply_nodes(self) -> set[int]:
        return self._expected_reply_nodes(self._target_node_id())

    def _reject_broadcast_read(self, operation: str) -> None:
        if self.target_axis == "broadcast":
            raise CtmError(f"{operation} does not support broadcast target; select left or right axis")

    def _with_target_axis(self, target_axis: str, callback):
        previous_target = self.target_axis
        self.set_target_axis(target_axis)
        try:
            return callback()
        finally:
            self.target_axis = previous_target


def config_item_by_index(index: int) -> ConfigItem:
    try:
        return CONFIG_BY_INDEX[index]
    except KeyError as exc:
        raise CtmError(f"未知参数序号：{index}") from exc


def load_firmware_image(path: str | Path) -> bytes:
    path = Path(path)
    if path.suffix.lower() == ".hex":
        return intel_hex_to_bin(path)
    return path.read_bytes()


def intel_hex_to_bin(path: Path, base_address: int = APP_BASE_ADDR) -> bytes:
    memory: dict[int, int] = {}
    upper = 0
    min_addr: int | None = None
    max_addr = 0

    for line_no, raw_line in enumerate(path.read_text(encoding="ascii").splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise CtmError(f"HEX 第 {line_no} 行无效：缺少 ':'")
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as exc:
            raise CtmError(f"HEX 第 {line_no} 行无效") from exc
        if len(record) < 5:
            raise CtmError(f"HEX 第 {line_no} 行无效：长度过短")

        length = record[0]
        offset = (record[1] << 8) | record[2]
        record_type = record[3]
        data = record[4 : 4 + length]
        checksum = sum(record) & 0xFF
        if checksum != 0:
            raise CtmError(f"HEX 第 {line_no} 行校验和错误")

        if record_type == 0x00:
            absolute = upper + offset
            for index, value in enumerate(data):
                addr = absolute + index
                if addr < base_address:
                    continue
                memory[addr] = value
                min_addr = addr if min_addr is None else min(min_addr, addr)
                max_addr = max(max_addr, addr)
        elif record_type == 0x01:
            break
        elif record_type == 0x02:
            if length != 2:
                raise CtmError(f"HEX 第 {line_no} 行段地址记录无效")
            upper = (((data[0] << 8) | data[1]) << 4) & 0xFFFFFFFF
        elif record_type == 0x04:
            if length != 2:
                raise CtmError(f"HEX 第 {line_no} 行线性地址记录无效")
            upper = ((data[0] << 8) | data[1]) << 16
        elif record_type in (0x03, 0x05):
            continue
        else:
            raise CtmError(f"HEX 第 {line_no} 行记录类型 {record_type} 暂不支持")

    if min_addr is None:
        raise CtmError("HEX 文件不包含应用区数据")
    if min_addr < base_address:
        raise CtmError(f"HEX 数据起始地址低于应用区基地址 0x{base_address:08X}")

    size = max_addr - base_address + 1
    if size <= 0:
        raise CtmError("HEX 文件不包含应用区基地址处的数据")
    image = bytearray(b"\xFF" * size)
    for addr, value in memory.items():
        image[addr - base_address] = value
    return bytes(image)
