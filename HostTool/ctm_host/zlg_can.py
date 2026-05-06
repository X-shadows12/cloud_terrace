from __future__ import annotations

from ctypes import POINTER, Structure, WinDLL, byref, c_int, c_ubyte, c_uint
from dataclasses import dataclass
from pathlib import Path
import sys
import time

from .protocol import CanMessage


STATUS_OK = 1
CAN_STANDARD_FRAME = 0
CAN_DATA_FRAME = 0


class VCI_CAN_OBJ(Structure):
    _fields_ = [
        ("ID", c_uint),
        ("TimeStamp", c_uint),
        ("TimeFlag", c_ubyte),
        ("SendType", c_ubyte),
        ("RemoteFlag", c_ubyte),
        ("ExternFlag", c_ubyte),
        ("DataLen", c_ubyte),
        ("Data", c_ubyte * 8),
        ("Reserved", c_ubyte * 3),
    ]


class VCI_INIT_CONFIG(Structure):
    _fields_ = [
        ("AccCode", c_uint),
        ("AccMask", c_uint),
        ("Reserved", c_uint),
        ("Filter", c_ubyte),
        ("Timing0", c_ubyte),
        ("Timing1", c_ubyte),
        ("Mode", c_ubyte),
    ]


@dataclass(frozen=True)
class ZlgDeviceType:
    code: int
    label: str


DEVICE_TYPES = [
    ZlgDeviceType(3, "USBCAN-I"),
    ZlgDeviceType(4, "USBCAN-II"),
    ZlgDeviceType(20, "USBCAN-E-U"),
    ZlgDeviceType(21, "USBCAN-2E-U"),
]


BIT_TIMING = {
    "250K": (0x01, 0x1C),
    "500K": (0x00, 0x1C),
    "800K": (0x00, 0x16),
    "1000K": (0x00, 0x14),
}


class ZlgCanError(RuntimeError):
    pass


class ZlgCanBus:
    def __init__(
        self,
        dll_path: str | Path = "ControlCAN.dll",
        device_type: int = 3,
        device_index: int = 0,
        channel_index: int = 0,
        bitrate: str = "500K",
    ) -> None:
        self.dll_path = str(dll_path)
        self.device_type = int(device_type)
        self.device_index = int(device_index)
        self.channel_index = int(channel_index)
        self.bitrate = bitrate
        self._dll = None
        self._open = False
        self._started = False

    @property
    def is_open(self) -> bool:
        return self._open and self._started

    def open(self) -> None:
        dll_path = self._resolve_dll_path(self.dll_path)
        try:
            self._dll = WinDLL(str(dll_path))
        except OSError as exc:
            raise ZlgCanError(
                f"无法加载 DLL：{dll_path}\n"
                "请确认 ControlCAN.dll 已放到程序同目录，且位数与程序一致。"
            ) from exc
        self._bind_signatures()

        if self._dll.VCI_OpenDevice(self.device_type, self.device_index, 0) != STATUS_OK:
            raise ZlgCanError("VCI_OpenDevice 打开设备失败")
        self._open = True

        cfg = self._make_init_config(self.bitrate)
        if self._dll.VCI_InitCAN(self.device_type, self.device_index, self.channel_index, byref(cfg)) != STATUS_OK:
            self.close()
            raise ZlgCanError("VCI_InitCAN 初始化 CAN 通道失败")

        self._dll.VCI_ClearBuffer(self.device_type, self.device_index, self.channel_index)

        if self._dll.VCI_StartCAN(self.device_type, self.device_index, self.channel_index) != STATUS_OK:
            self.close()
            raise ZlgCanError("VCI_StartCAN 启动 CAN 通道失败")
        self._started = True

    def close(self) -> None:
        if self._dll is not None and self._open:
            self._dll.VCI_CloseDevice(self.device_type, self.device_index)
        self._started = False
        self._open = False

    def send(self, message: CanMessage) -> None:
        self._require_open()
        frame = VCI_CAN_OBJ()
        frame.ID = message.can_id & 0x7FF
        frame.SendType = 0
        frame.RemoteFlag = CAN_DATA_FRAME
        frame.ExternFlag = CAN_STANDARD_FRAME
        frame.DataLen = min(len(message.data), 8)
        for index, value in enumerate(message.data[:8]):
            frame.Data[index] = value

        sent = self._dll.VCI_Transmit(
            self.device_type,
            self.device_index,
            self.channel_index,
            byref(frame),
            1,
        )
        if sent != 1:
            raise ZlgCanError("VCI_Transmit 发送失败")

    def recv(self, timeout_ms: int = 50) -> CanMessage | None:
        self._require_open()
        deadline = time.monotonic() + timeout_ms / 1000.0
        frame = VCI_CAN_OBJ()

        while True:
            count = self._dll.VCI_Receive(
                self.device_type,
                self.device_index,
                self.channel_index,
                byref(frame),
                1,
                0,
            )
            if count == 1:
                return self._frame_to_message(frame)
            if time.monotonic() >= deadline:
                return None
            time.sleep(0.001)

    def drain(self, limit: int = 200) -> list[CanMessage]:
        self._require_open()
        messages: list[CanMessage] = []
        for _ in range(limit):
            msg = self.recv(timeout_ms=0)
            if msg is None:
                break
            messages.append(msg)
        return messages

    def _require_open(self) -> None:
        if not self.is_open:
            raise ZlgCanError("CAN 设备尚未打开")

    def _bind_signatures(self) -> None:
        assert self._dll is not None
        self._dll.VCI_OpenDevice.argtypes = [c_uint, c_uint, c_uint]
        self._dll.VCI_OpenDevice.restype = c_uint
        self._dll.VCI_CloseDevice.argtypes = [c_uint, c_uint]
        self._dll.VCI_CloseDevice.restype = c_uint
        self._dll.VCI_InitCAN.argtypes = [c_uint, c_uint, c_uint, POINTER(VCI_INIT_CONFIG)]
        self._dll.VCI_InitCAN.restype = c_uint
        self._dll.VCI_StartCAN.argtypes = [c_uint, c_uint, c_uint]
        self._dll.VCI_StartCAN.restype = c_uint
        self._dll.VCI_ClearBuffer.argtypes = [c_uint, c_uint, c_uint]
        self._dll.VCI_ClearBuffer.restype = c_uint
        self._dll.VCI_Transmit.argtypes = [c_uint, c_uint, c_uint, POINTER(VCI_CAN_OBJ), c_uint]
        self._dll.VCI_Transmit.restype = c_uint
        self._dll.VCI_Receive.argtypes = [c_uint, c_uint, c_uint, POINTER(VCI_CAN_OBJ), c_uint, c_int]
        self._dll.VCI_Receive.restype = c_uint

    def _make_init_config(self, bitrate: str) -> VCI_INIT_CONFIG:
        if bitrate not in BIT_TIMING:
            raise ZlgCanError(f"不支持的波特率：{bitrate}")
        timing0, timing1 = BIT_TIMING[bitrate]
        cfg = VCI_INIT_CONFIG()
        cfg.AccCode = 0x00000000
        cfg.AccMask = 0xFFFFFFFF
        cfg.Reserved = 0
        cfg.Filter = 1
        cfg.Timing0 = timing0
        cfg.Timing1 = timing1
        cfg.Mode = 0
        return cfg

    @staticmethod
    def _resolve_dll_path(dll_path: str | Path) -> Path:
        path = Path(dll_path).expanduser()
        if path.is_absolute():
            return path

        candidates = [Path.cwd() / path]
        if getattr(sys, "frozen", False):
            candidates.append(Path(sys.executable).resolve().parent / path)
        else:
            candidates.append(Path(__file__).resolve().parents[1] / path)

        for candidate in candidates:
            if candidate.exists():
                return candidate
        return path

    @staticmethod
    def _frame_to_message(frame: VCI_CAN_OBJ) -> CanMessage:
        return CanMessage(
            can_id=frame.ID & 0x7FF,
            data=bytes(frame.Data[index] for index in range(min(frame.DataLen, 8))),
        )
