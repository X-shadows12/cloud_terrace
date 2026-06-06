# CAN 分析仪双电机测试说明

这份文档汇总了 CTM 双电机固件的 CAN 协议，方便直接使用 CAN 分析仪进行收发测试，不依赖 HostTool。

## 帧格式

- 标准 CAN，11 位 ID
- 请求帧 ID 格式：
  `can_id = (node_id << 5) | cmd`
- 回复帧 ID 格式：
  `can_id = 0x400 | (node_id << 5) | cmd`
- `node_id` 占 5 bit
- `cmd` 占 5 bit
- 数据为小端
- 浮点使用 IEEE754 `float32`，小端

## 双电机节点号规则

- 左电机节点号 = `UsrConfig.node_id`
- 右电机节点号 = `UsrConfig.node_id + 1`
- 广播节点号 = `0`

默认配置下：

- 左电机节点号 = `1`
- 右电机节点号 = `2`
- 广播节点号 = `0`

## 命令 ID

| 命令 | ID | 说明 |
| --- | ---: | --- |
| `SET_OP_MODE` | `0` | 1 字节载荷 |
| `MOTOR_ENABLE` | `1` | 无载荷 |
| `MOTOR_DISABLE` | `2` | 无载荷 |
| `SET_TORQUE` | `3` | `float32` 电流目标 |
| `SET_VELOCITY` | `4` | `float32` 速度目标 |
| `SET_POSITION` | `5` | `float32` 位置目标 |
| `SYNC` | `6` | 无载荷 |
| `CALIB_START` | `7` | 无载荷 |
| `CALIB_ABORT` | `9` | 无载荷 |
| `ANTICOGGING_START` | `10` | 无载荷 |
| `ANTICOGGING_ABORT` | `12` | 无载荷 |
| `SET_HOME` | `13` | 无载荷 |
| `ERROR_RESET` | `14` | 无载荷 |
| `GET_STATUSWORD` | `15` | 无载荷 |
| `GET_VALUE_1` | `17` | 1 字节索引 |
| `GET_VALUE_2` | `18` | 1 字节索引 |
| `SET_CONFIG` | `24` | 8 字节 |
| `GET_CONFIG` | `25` | 8 字节 |
| `SAVE_ALL_CONFIG` | `26` | 无载荷 |
| `RESET_ALL_CONFIG` | `27` | 无载荷 |
| `GET_FW_VERSION` | `28` | 无载荷 |

## 控制模式取值

| 模式 | 数值 |
| --- | ---: |
| 电流爬坡 | `0` |
| 速度爬坡 | `1` |
| 位置滤波 | `2` |
| 梯形位置规划 | `3` |

## 实时量索引

用于 `GET_VALUE_1` 和 `GET_VALUE_2`。

| 索引 | 含义 |
| --- | --- |
| `0` | Iq 电流 |
| `1` | 速度 |
| `2` | 位置 |
| `3` | Vbus |
| `4` | Ibus |
| `5` | 功率 |
| `6` | 驱动温度 |
| `7` | NTC 温度 |

## ACK 规则

下面这些命令会返回 1 字节 ACK：

- `SET_OP_MODE`
- `MOTOR_ENABLE`
- `MOTOR_DISABLE`
- `CALIB_START`
- `CALIB_ABORT`
- `ANTICOGGING_START`
- `ANTICOGGING_ABORT`
- `SET_HOME`
- `ERROR_RESET`
- `SAVE_ALL_CONFIG`
- `RESET_ALL_CONFIG`

ACK 数据定义：

- `00` 成功
- `EE` 失败

`SET_TORQUE`、`SET_VELOCITY`、`SET_POSITION`、`SYNC` 默认不返回 ACK。

## 常用浮点编码示例

| 数值 | `float32` 小端字节 |
| --- | --- |
| `0.0` | `00 00 00 00` |
| `0.5` | `00 00 00 3F` |
| `1.0` | `00 00 80 3F` |
| `2.0` | `00 00 00 40` |
| `10.0` | `00 00 20 41` |
| `-1.0` | `00 00 80 BF` |

## 快速测试脚本

默认节点号假设为：

- 左轴 = `1`
- 右轴 = `2`

### 1. 把左右电机都设为梯形位置模式

- 左轴：
  `ID=0x20 DLC=1 DATA=03`
- 右轴：
  `ID=0x40 DLC=1 DATA=03`

预期 ACK：

- 左轴回复：`ID=0x420 DLC=1 DATA=00`
- 右轴回复：`ID=0x440 DLC=1 DATA=00`

### 2. 使能左右电机

可以分别发：

- 左轴：
  `ID=0x21 DLC=0`
- 右轴：
  `ID=0x41 DLC=0`

也可以广播一起使能：

- 双轴广播：
  `ID=0x01 DLC=0`

预期 ACK：

- 左轴回复：`ID=0x421 DLC=1 DATA=00`
- 右轴回复：`ID=0x441 DLC=1 DATA=00`

说明：
当前固件可能在进入 `RUN` 时自动执行一次 `home`。

### 3. 发送位置指令

给左右轴不同目标：

- 左轴到 `1.0 rev`：
  `ID=0x25 DLC=4 DATA=00 00 80 3F`
- 右轴到 `2.0 rev`：
  `ID=0x45 DLC=4 DATA=00 00 00 40`

如果两个轴目标一样，也可以直接广播：

- 两轴同时到 `1.0 rev`：
  `ID=0x05 DLC=4 DATA=00 00 80 3F`

### 4. 可选的 SYNC

如果 `sync_target_enable = 1`，顺序应该是：

1. 发左轴目标
2. 发右轴目标
3. 再发 `SYNC`

SYNC 帧：

- `ID=0x06 DLC=0`

如果 `sync_target_enable = 0`，目标会立即生效，不需要额外发 `SYNC`。

### 5. 读取状态字

- 左轴请求：
  `ID=0x2F DLC=0`
- 右轴请求：
  `ID=0x4F DLC=0`

预期回复：

- 左轴回复：
  `ID=0x42F DLC=2 DATA=<status_code> <errors_code>`
- 右轴回复：
  `ID=0x44F DLC=2 DATA=<status_code> <errors_code>`

`status_code` 位定义：

- bit0：已使能
- bit1：目标到达

`errors_code` 位定义：

- bit0：过压
- bit1：欠压
- bit2：过流
- bit3：驱动过温
- bit4：NTC 过温
- bit7：自检错误

### 6. 读取实时位置

- 左轴请求：
  `ID=0x31 DLC=1 DATA=02`
- 右轴请求：
  `ID=0x51 DLC=1 DATA=02`

预期回复：

- 左轴回复：
  `ID=0x431 DLC=4 DATA=<float32 position>`
- 右轴回复：
  `ID=0x451 DLC=4 DATA=<float32 position>`

### 7. 设零点

- 左轴：
  `ID=0x2D DLC=0`
- 右轴：
  `ID=0x4D DLC=0`
- 广播：
  `ID=0x0D DLC=0`

预期 ACK：

- 左轴回复：`ID=0x42D DLC=1 DATA=00`
- 右轴回复：`ID=0x44D DLC=1 DATA=00`

### 8. 失能左右电机

- 广播失能：
  `ID=0x02 DLC=0`

或者分别发：

- 左轴：
  `ID=0x22 DLC=0`
- 右轴：
  `ID=0x42 DLC=0`

预期 ACK：

- 左轴回复：`ID=0x422 DLC=1 DATA=00`
- 右轴回复：`ID=0x442 DLC=1 DATA=00`

## 最简手工测试顺序

如果你只想快速把双电机跑起来，可以直接按下面顺序发：

1. `ID=0x20 DLC=1 DATA=03`
2. `ID=0x40 DLC=1 DATA=03`
3. `ID=0x01 DLC=0`
4. `ID=0x25 DLC=4 DATA=00 00 80 3F`
5. `ID=0x45 DLC=4 DATA=00 00 00 40`

如果启用了同步目标，再补一帧：

6. `ID=0x06 DLC=0`
