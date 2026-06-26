# Mini TCP 协议栈深度解析

> 参考 MIT 6.829, Stanford CS144, RFC 793/5681/6298/7323

## 概述

本项目实现了一个简化的、基于内存的 TCP 协议栈模拟。虽然不经过真实网络栈，但完整模拟了 TCP 核心机制：三次握手、四次挥手、滑动窗口、超时重传、拥塞控制。

## 1. TCP 状态机

```
                              +---------+ ---------\      active OPEN
                              |  CLOSED |            \    -----------
                              +---------+<---------\   \   create TCB
                                |     ^              \   \  snd SYN
                   passive OPEN |     |   CLOSE        \   \
                   ------------ |     | ----------       \   \
                    create TCB  |     | delete TCB         \   \
                                V     |                      \   \
                              +---------+            CLOSE    |    \
                              |  LISTEN |          ---------- |     |
                              +---------+          delete TCB |     |
                   rcv SYN      |     |     SEND              |     |
                  -----------   |     |    -------            |     V
 +---------+      snd SYN,ACK  /       \   snd SYN          +---------+
 |         |<-----------------           ------------------>|         |
 |   SYN   |                   CLOSE                   |    |   SYN   |
 |   RCVD  |<-----------------------------------------       |   SENT  |
 |         |                    rcv SYN                     |         |
 |         |----                 ------                     |         |
 +---------+                    snd ACK                     +---------+
     |                                        |
     | rcv ACK of SYN                        | rcv SYN,ACK
     | --------------                        | ------------
     | x         |                           |        x
     |           |                           |
     V           V                           V
 +---------+                        +---------+
 |         |       CLOSE             |         |
 |  ESTAB  |<----------------------- |  ESTAB  |
 |         |  ---------              |         |
 +---------+  snd FIN     +---------+         |
     |        ----------->|         |--------->
     |                    |  CLOSE  |  rcv FIN
     |    rcv FIN         |  WAIT   | -------
     |  -------           +---------+  snd ACK
     V      x                                 V
 +---------+        +---------+        +---------+
 |  CLOSE  |        |  CLOSING|        |  LAST   |
 |  WAIT   |        +---------+        |  ACK    |
 +---------+            |  rcv ACK     +---------+
     |  CLOSE           | ---------       |
     | ------           |       x         |
     V                  V       V         V
 +---------+        +---------+        +---------+
 |  LAST   |        |  TIME   |        |  CLOSED |
 |  ACK    |        |  WAIT   |        +---------+
 +---------+        +---------+
     |                   |
     | rcv ACK           | Timeout (2MSL)
     | ------            | --------------
     V                   V
 +---------+        +---------+
 |  CLOSED |        |  CLOSED |
 +---------+        +---------+
```

### 三次握手 (3-way Handshake)

1. **SYN (Client→Server)**: 客户端发送 SYN，seq=x，状态变为 SYN_SENT
2. **SYN-ACK (Server→Client)**: 服务器回复 SYN+ACK，seq=y，ack=x+1，状态变为 SYN_RECEIVED
3. **ACK (Client→Server)**: 客户端发送 ACK，seq=x+1，ack=y+1，双方状态变为 ESTABLISHED

### 四次挥手 (4-way Close)

1. **FIN (主动方→被动方)**: 主动方发送 FIN，状态变为 FIN_WAIT_1
2. **ACK (被动方→主动方)**: 被动方确认，状态变为 CLOSE_WAIT；主动方收到ACK，状态变为 FIN_WAIT_2
3. **FIN (被动方→主动方)**: 被动方调用 close，发送 FIN，状态变为 LAST_ACK
4. **ACK (主动方→被动方)**: 主动方确认，状态变为 TIME_WAIT (等待2MSL)；被动方收到ACK，状态变为 CLOSED

### TIME_WAIT 状态

TIME_WAIT 持续 2*MSL (Maximum Segment Lifetime，通常 60-120 秒)：
- 确保最后的 ACK 能到达对方（如果 ACK 丢失，对方会重传 FIN）
- 让旧连接的所有报文在网络中消失，避免与新连接混淆

## 2. 滑动窗口 (Sliding Window)

### 发送窗口

```
                   发送但未确认                 可发送             不可发送
              |<----- SND.WND ----->|
              |                      |
  ...ACKed...|1|2|3|4|5|6|7|8|9|10|...|...|...|...
              |  |                 |              |
             SND.UNA            SND.NXT    SND.UNA+SND.WND
            (已确认上限)        (下一个发送序号)  (窗口上限)
```

- **SND.UNA**: 最早的未确认字节序号 (Send Unacknowledged)
- **SND.NXT**: 下一个要发送的字节序号 (Send Next)
- **SND.WND**: 发送窗口大小 (由接收方通告的 rwnd 决定)

可用窗口 = SND.WND - (SND.NXT - SND.UNA)

### 接收窗口

```
                   已接收并确认          可接收            不可接收
              |<------ RCV.WND ------>|
              |                        |
  ...ACKed...|1|2|3|4|5|6|7|8|9|10|...|...|...|...
              |  |                    |              |
            RCV.NXT                RCV.NXT+RCV.WND
          (期望下一个序号)
```

- **RCV.NXT**: 期望收到的下一个字节序号

### 窗口更新与零窗口探测

接收方必须通告窗口大小(rwnd)以避免发送方溢出接收缓冲区。当 rwnd=0 时：
- 发送方停止发送数据
- 发送方启动"零窗口探测定时器"(persist timer)
- 定期发送窗口探测报文(1字节数据)

## 3. 超时重传 (Retransmission)

### RTT 估计 (RFC 6298)

```
SRTT = (1 - α) * SRTT + α * RTT_sample    // α = 1/8
RTTVAR = (1 - β) * RTTVAR + β * |SRTT - RTT_sample|  // β = 1/4
RTO = SRTT + max(G, 4 * RTTVAR)           // G = 时钟粒度
```

初始值：
- 首次 RTO = 1000ms (1秒)
- SRTT 未初始化时，取第一个 RTT_sample
- RTTVAR = RTT_sample / 2

### Karn 算法

重传的报文段的 RTT 测量不可信(无法区分是对原始报文还是重传报文的确认)。对策：
1. 不测量重传报文的 RTT
2. 重传时使用指数退避：RTO = RTO * 2 (最大 60s)
3. 只有当收到非重传报文的确认时，才重新使用 Jacobson 算法计算 RTO

### 快速重传 (Fast Retransmit)

收到 3 个重复 ACK (dup ACK = 4 个相同 ACK)，立即重传丢失的报文，而不等待 RTO 超时。

```
发送方发送: 1 2 3 4 5
接收方收到: 1 (ACK 2)
            3 (dup ACK 2) -- 失序
            4 (dup ACK 2) -- 第1个dup
            5 (dup ACK 2) -- 第2个dup
            -- 第3个dup ACK --
发送方: 快速重传报文 2
```

## 4. 拥塞控制 (Congestion Control)

### TCP Reno 算法

#### 核心变量

| 变量 | 含义 | 初始值 |
|------|------|--------|
| cwnd | 拥塞窗口 | 1 MSS |
| ssthresh | 慢启动阈值 | 65535 字节 (通常较大) |
| rwnd | 接收方通告窗口 | 由对方通告 |
| FlightSize | 在途数据量 | 0 |

**有效窗口 = min(cwnd, rwnd)**

#### 慢启动 (Slow Start)

```
每收到一个 ACK: cwnd += 1 MSS
每经过一个 RTT: cwnd *= 2 (指数增长)
cwnd >= ssthresh 时: 进入拥塞避免
```

#### 拥塞避免 (Congestion Avoidance)

```
每 RTT: cwnd += 1 MSS (线性增长)
实际上每个 ACK: cwnd += MSS * (MSS / cwnd)
```

#### 丢包检测与反应

**超时 (RTO Timeout)**:
```
ssthresh = max(FlightSize/2, 2*MSS)
cwnd = 1 MSS
进入慢启动
```

**3个重复 ACK (Fast Retransmit/Fast Recovery)**:
```
ssthresh = max(FlightSize/2, 2*MSS)
cwnd = ssthresh + 3*MSS
重传丢失的报文
进入快速恢复
```

#### 快速恢复 (Fast Recovery)

```
每收到一个重复 ACK: cwnd += 1 MSS
收到新 ACK 时: cwnd = ssthresh，退出快速恢复，进入拥塞避免
```

### 状态图

```
                        +-----------+
                        |   START   |
                        +-----------+
                        | cwnd=1 MSS
                        v
                  +--------------+         cwnd >= ssthresh
                  |  SLOW START  |----------------------------+
                  +--------------+                            |
                  指数增长: cwnd *= 2                          |
                  |     每个RTT                                v
                  |               丢包(超时)           +-----------------+
                  +----------+  ssthresh=cwnd/2       | CONGESTION      |
                  |          |  cwnd=1 MSS            | AVOIDANCE       |
                  | 丢包(3个  +---> 重新进入慢启动     +-----------------+
                  | dup ACK)                          线性增长: cwnd += 1 MSS
                  | ssthresh=cwnd/2                          每个RTT
                  | cwnd=ssthresh+3                            |
                  v                                            |
           +-------------+         收到新ACK                    | 丢包(3dup)
           |   FAST      |  cwnd = ssthresh                    |
           |   RECOVERY  |<------------------------------------+
           +-------------+ 进入拥塞避免
              |
              | 收到dup ACK
              v
           cwnd += 1 MSS
```

## 5. 报文段格式 (TCP Segment Format)

```
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |          Source Port          |       Destination Port        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        Sequence Number                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                    Acknowledgment Number                      |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  Data |           |U|A|P|R|S|F|                               |
   | Offset| Reserved  |R|C|S|S|Y|I|        Window                |
   |       |           |G|K|H|T|N|N|                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           Checksum            |         Urgent Pointer        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                    Options                    |    Padding    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                             data                              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

标志位: URG(紧急)、ACK(确认)、PSH(推送)、RST(复位)、SYN(同步)、FIN(结束)

## 6. 关键参考

- **RFC 793**: Transmission Control Protocol (原始规范)
- **RFC 1122**: Requirements for Internet Hosts
- **RFC 1323**: TCP Extensions for High Performance (窗口缩放、时间戳)
- **RFC 2018**: TCP Selective Acknowledgment Options (SACK)
- **RFC 2581**: TCP Congestion Control
- **RFC 5681**: TCP Congestion Control (更新)
- **RFC 6298**: Computing TCP's Retransmission Timer
- **RFC 7323**: TCP Extensions for High Performance (更新)
- **MIT 6.829**: Computer Networks (Lectures 6-10)
- **Stanford CS144**: Introduction to Computer Networking (Lectures 3-7)
- **UNP Vol 1**: Stevens, UNIX Network Programming

## 7. 本实现局限

本迷你 TCP 栈的模拟实现仅用于教学目的，与现实 TCP 栈的主要差异：

1. **无真实网络 I/O**: 所有通信在内存中模拟，无 socket 系统调用
2. **无拥塞控制实现**: 定义了接口但未完整实现 Reno
3. **无 SACK (Selective ACK)**: 仅支持累积 ACK
4. **无 Nagle 算法**: 未实现小包合并（TCP_NODELAY 相关）
5. **无 Keep-Alive**: 未实现 TCP keep-alive 机制
6. **有限的状态转移**: 部分边缘状态转移未覆盖
7. **无 PAWS (Protection Against Wrapped Sequence)**: 未使用时间戳选项

建议进一步学习时参考 Linux 内核 tcp_input.c/tcp_output.c 的实现。
