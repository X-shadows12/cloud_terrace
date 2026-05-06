from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct


ID_ECHO_BIT = 0x400
ID_NODE_BIT = 0x3E0
ID_CMD_BIT = 0x01F


class Command(IntEnum):
    SET_OP_MODE = 0
    MOTOR_ENABLE = 1
    MOTOR_DISABLE = 2
    SET_TORQUE = 3
    SET_VELOCITY = 4
    SET_POSITION = 5
    SYNC = 6
    CALIB_START = 7
    CALIB_REPORT = 8
    CALIB_ABORT = 9
    ANTICOGGING_START = 10
    ANTICOGGING_REPORT = 11
    ANTICOGGING_ABORT = 12
    SET_HOME = 13
    ERROR_RESET = 14
    GET_STATUSWORD = 15
    STATUSWORD_REPORT = 16
    GET_VALUE_1 = 17
    GET_VALUE_2 = 18
    HEARTBEAT = 23
    SET_CONFIG = 24
    GET_CONFIG = 25
    SAVE_ALL_CONFIG = 26
    RESET_ALL_CONFIG = 27
    GET_FW_VERSION = 28
    DFU_START = 29
    DFU_DATA = 30
    DFU_END = 31


class ControlMode(IntEnum):
    CURRENT_RAMP = 0
    VELOCITY_RAMP = 1
    POSITION_FILTER = 2
    POSITION_PROFILE = 3


CONTROL_MODE_LABELS = {
    ControlMode.CURRENT_RAMP: "电流爬坡",
    ControlMode.VELOCITY_RAMP: "速度爬坡",
    ControlMode.POSITION_FILTER: "位置滤波",
    ControlMode.POSITION_PROFILE: "梯形位置规划",
}


CAN_BAUDRATE_CONFIG_VALUES = {
    "250K": 0,
    "500K": 1,
    "800K": 2,
    "1000K": 3,
}


@dataclass(frozen=True)
class CanMessage:
    can_id: int
    data: bytes = b""

    @property
    def dlc(self) -> int:
        return len(self.data)


@dataclass(frozen=True)
class ParsedId:
    echo: bool
    node_id: int
    command: int


@dataclass(frozen=True)
class ConfigItem:
    index: int
    key: str
    label: str
    value_type: str
    unit: str = ""
    note: str = ""


@dataclass(frozen=True)
class ValueItem:
    index: int
    key: str
    label: str
    unit: str = ""


CONFIG_ITEMS = [
    ConfigItem(1, "invert_motor_dir", "电机方向取反", "int", note="0 否，1 是"),
    ConfigItem(2, "motor_pole_pairs", "电机极对数", "int", "PP"),
    ConfigItem(3, "motor_phase_resistance", "相电阻", "float", "ohm"),
    ConfigItem(4, "motor_phase_inductance", "相电感", "float", "H"),
    ConfigItem(5, "current_limit", "电流限制", "float", "A"),
    ConfigItem(6, "velocity_limit", "速度限制", "float", "r/s"),
    ConfigItem(7, "calib_current", "校准电流", "float", "A"),
    ConfigItem(8, "calib_voltage", "校准电压", "float", "V"),
    ConfigItem(9, "pos_p_gain", "位置环 P 增益", "float"),
    ConfigItem(10, "vel_p_gain", "速度环 P 增益", "float"),
    ConfigItem(11, "vel_i_gain", "速度环 I 增益", "float"),
    ConfigItem(12, "current_ff_gain", "电流前馈增益", "float", "A/(r/s^2)"),
    ConfigItem(13, "current_ctrl_bw", "电流环带宽", "float", "Hz"),
    ConfigItem(14, "default_op_mode", "默认控制模式", "int", note="0 电流，1 速度，2 位置滤波，3 梯形位置"),
    ConfigItem(15, "anticogging_enable", "齿槽补偿使能", "int", note="0 否，1 是"),
    ConfigItem(16, "sync_target_enable", "目标同步使能", "int", note="0 否，1 是"),
    ConfigItem(17, "target_velcity_window", "到达速度窗口", "float", "r/s"),
    ConfigItem(18, "target_position_window", "到达位置窗口", "float", "r"),
    ConfigItem(19, "current_ramp_rate", "电流爬坡斜率", "float", "A/s"),
    ConfigItem(20, "velocity_ramp_rate", "速度爬坡斜率", "float", "r/s^2"),
    ConfigItem(21, "position_filter_bw", "位置滤波带宽", "float", "Hz"),
    ConfigItem(22, "profile_velocity", "规划速度", "float", "r/s"),
    ConfigItem(23, "profile_accel", "规划加速度", "float", "r/s^2"),
    ConfigItem(24, "profile_decel", "规划减速度", "float", "r/s^2"),
    ConfigItem(25, "protect_under_voltage", "欠压保护阈值", "float", "V"),
    ConfigItem(26, "protect_over_voltage", "过压保护阈值", "float", "V"),
    ConfigItem(27, "protect_over_current", "过流保护阈值", "float", "A"),
    ConfigItem(28, "protect_drv_over_tmp", "驱动过温阈值", "int", "deg C"),
    ConfigItem(29, "protect_ntc_over_tmp", "NTC 过温阈值", "int", "deg C"),
    ConfigItem(30, "node_id", "CAN 节点 ID", "int", note="1..31，固件接收 0 作为广播"),
    ConfigItem(31, "can_baudrate", "CAN 波特率枚举", "int", note="0 250K，1 500K，2 800K，3 1000K"),
    ConfigItem(32, "heartbeat_consumer_ms", "心跳消费超时", "int", "ms"),
    ConfigItem(33, "heartbeat_producer_ms", "心跳发送周期", "int", "ms"),
]

CONFIG_BY_INDEX = {item.index: item for item in CONFIG_ITEMS}
CONFIG_BY_KEY = {item.key: item for item in CONFIG_ITEMS}


VALUE_ITEMS = [
    ValueItem(0, "iq", "Iq 电流", "A"),
    ValueItem(1, "velocity", "速度", "r/s"),
    ValueItem(2, "position", "位置", "r"),
    ValueItem(3, "vbus", "母线电压", "V"),
    ValueItem(4, "ibus", "母线电流", "A"),
    ValueItem(5, "power", "功率", "W"),
    ValueItem(6, "driver_temp", "驱动温度", "deg C"),
    ValueItem(7, "ntc_temp", "NTC 温度", "deg C"),
]

VALUE_BY_INDEX = {item.index: item for item in VALUE_ITEMS}


STATUS_BITS = [
    (0, "已使能"),
    (1, "目标到达"),
]

ERROR_BITS = [
    (0, "过压"),
    (1, "欠压"),
    (2, "过流"),
    (3, "驱动过温"),
    (4, "NTC过温"),
    (7, "自检错误"),
]


def make_can_id(node_id: int, command: int | Command, echo: bool = False) -> int:
    if not 0 <= node_id <= 31:
        raise ValueError("node_id must be in range 0..31")
    cmd = int(command)
    if not 0 <= cmd <= 31:
        raise ValueError("command must be in range 0..31")
    return (ID_ECHO_BIT if echo else 0) | ((node_id & 0x1F) << 5) | (cmd & ID_CMD_BIT)


def parse_can_id(can_id: int) -> ParsedId:
    return ParsedId(
        echo=bool(can_id & ID_ECHO_BIT),
        node_id=(can_id & ID_NODE_BIT) >> 5,
        command=can_id & ID_CMD_BIT,
    )


def pack_float(value: float) -> bytes:
    return struct.pack("<f", float(value))


def unpack_float(data: bytes) -> float:
    return struct.unpack("<f", bytes(data[:4]).ljust(4, b"\x00"))[0]


def pack_i32(value: int) -> bytes:
    return struct.pack("<i", int(value))


def unpack_i32(data: bytes) -> int:
    return struct.unpack("<i", bytes(data[:4]).ljust(4, b"\x00"))[0]


def pack_u32(value: int) -> bytes:
    return struct.pack("<I", int(value) & 0xFFFFFFFF)


def unpack_u32(data: bytes) -> int:
    return struct.unpack("<I", bytes(data[:4]).ljust(4, b"\x00"))[0]


def pack_config_value(item: ConfigItem, value: str | int | float) -> bytes:
    if item.value_type == "float":
        return pack_float(float(value))
    return pack_i32(int(value))


def unpack_config_value(item: ConfigItem, data: bytes) -> int | float:
    if item.value_type == "float":
        return unpack_float(data)
    return unpack_i32(data)


def decode_status(status_code: int, errors_code: int) -> tuple[list[str], list[str]]:
    status = [name for bit, name in STATUS_BITS if status_code & (1 << bit)]
    errors = [name for bit, name in ERROR_BITS if errors_code & (1 << bit)]
    return status, errors


def ctm_crc32(data: bytes) -> int:
    """固件 DFU 使用的 CRC32：多项式 0x04C11DB7，初值 0。"""
    crc = 0
    for byte in data:
        idx = ((crc >> 24) ^ byte) & 0xFF
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC32_TABLE[idx]
    return crc & 0xFFFFFFFF


def _build_crc32_table() -> tuple[int, ...]:
    table: list[int] = []
    poly = 0x04C11DB7
    for value in range(256):
        crc = value << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ poly) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
        table.append(crc)
    return tuple(table)


_CRC32_TABLE = _build_crc32_table()
