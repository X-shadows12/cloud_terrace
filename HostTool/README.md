# CTM 上位机（ZLG USB CAN）

这是为当前 GD32H759 版本 CTM 固件重做的上位机。程序使用 Python 标准库的 Tkinter 图形界面，并通过 ZLG 的 `ControlCAN.dll` 访问 USB CAN 适配器。

## 运行源码版

1. 安装 64 位 Python，并确保位数和 ZLG 驱动/DLL 一致。
2. 把 `ControlCAN.dll` 放到 `CTMHostTool.exe` 同目录，或者在界面里手动选择 DLL。
3. 将 ZLG USB CAN 适配器接到驱动板 J1 的 CANH、CANL、GND。
4. 启动程序：

```powershell
cd D:\Learing_Materials\dgm\HostTool
python run_ctm_host.py
```

默认连接参数和固件默认值一致：

- 设备类型：`USBCAN-I`（`3`）
- 设备号：`0`
- 通道：`0`
- CAN 波特率：`500K`
- 节点 ID：`1`

如果你用的是 USBCAN-II，设备类型选 `4`；如果是较新的 USBCAN-2E-U 驱动，设备类型可尝试 `21`。

## 已实现功能

- 连接/断开 ZLG USB CAN。
- 读取固件版本、状态字和实时数据。
- 设置控制模式。
- 电机使能/失能。
- 下发电流、速度、位置目标。
- 同步下发目标。
- 设置当前位置为零点，清除非致命错误。
- 读取/写入固件开放的 33 个用户参数。
- 保存参数和齿槽补偿表到 Flash。
- 恢复固件默认参数。
- 启动/中止自动校准和齿槽补偿。
- 使用 `.bin` 或 Intel HEX `.hex` 文件进行 DFU 升级。

## 协议说明

固件使用 classic CAN，11 位标准帧 ID：

```text
bit10   echo 标志
bit9..5 节点 ID，1..31，固件接收 0 作为广播
bit4..0 命令 ID
```

CAN payload 使用小端格式。固件直接把 C 里的 `int32_t`、`uint32_t` 和 `float` 复制到 8 字节 payload。

## 安全注意

- 程序连接后不会自动使能电机。
- 第一次联调时，先测试读取状态和参数，再发送运动命令。
- DFU 文件大小限制为固件应用区的 256 KB。
- 修改 `node_id` 或 `can_baudrate` 后，需要保存到 Flash，并在设备重启后用新参数重新连接。
