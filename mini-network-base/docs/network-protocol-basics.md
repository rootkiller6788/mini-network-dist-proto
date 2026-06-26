# 网络协议基础 — TCP/IP, DNS, TLS, HTTP

> 计算机网络核心协议概述，适用于自学者和本科生参考

## 1. TCP/IP 协议栈

### OSI 七层模型 vs TCP/IP 四层模型

```
OSI Model                    TCP/IP Model           Protocols           本模块

+-----------+              +---------------+     +-------------+    +--------------+
| Application|             |               |     | HTTP, DNS,  |    | http_basic   |
+-----------+              |  Application  |     | TLS, SMTP,  |    | udp_dns      |
| Presentation|            |               |     | FTP, SSH    |    | tls_handshake|
+-----------+              +---------------+     +-------------+    +--------------+
| Session    |             |               |
+-----------+              |  Transport    |     | TCP, UDP    |    | socket_tcp   |
| Transport  |             |               |     |             |    | udp_dns      |
+-----------+              +---------------+     +-------------+    +--------------+
| Network    |             |  Internet     |     | IPv4, IPv6  |    | ip_packet    |
+-----------+              +---------------+     +-------------+    +--------------+
| Data Link  |             |  Link /       |     | Ethernet,   |    | (未实现)      |
+-----------+              |  Physical     |     | Wi-Fi       |    |              |
| Physical   |             |               |     |             |    |              |
+-----------+              +---------------+     +-------------+    +--------------+
```

### 封装 (Encapsulation)

每个协议层在被传递到下层之前都会添加自己的头部(和尾部)：

```
[ Ethernet Header | IP Header | TCP Header | HTTP Data | Ethernet Trailer ]
  14 bytes          20 bytes    20 bytes    variable    4 bytes (FCS)

以太网帧最大载荷 (MTU): 1500 bytes
TCP MSS (Maximum Segment Size) = MTU - IP Header - TCP Header
                               = 1500 - 20 - 20 = 1460 bytes
```

### IP 地址表示

IPv4 通过 32 位无符号整数表示：
```
127.0.0.1  = 0x7F000001 = 2130706433
192.168.1.1 = 0xC0A80101 = 3232235777
```

子网掩码: `255.255.255.0 = /24`, 网络部分 192.168.1, 主机部分 .1

## 2. IP 协议 (Internet Protocol)

### IPv4 头部结构

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Version|  IHL  |Type of Service|          Total Length         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Identification        |Flags|      Fragment Offset    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Time to Live |    Protocol   |         Header Checksum       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Source Address                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Destination Address                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Options (if IHL > 5)       |    Padding    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### IP 分片 (Fragmentation)

当 IP 数据包大小超过链路层 MTU 时，路由器或发送方将其分割为较小的片段：

- 每个片段携带原始 IP 头部 (部分字段被修改)
- **Identification**: 原始数据包的 ID，所有片段共享
- **Fragment Offset**: 片段数据在原始数据包中的位置 (乘以 8)
- **Flags**: `DF` (Don't Fragment), `MF` (More Fragments)
- 最终目标主机负责重组所有片段

### IP 校验和 (One's Complement Checksum)

```
1. 将 IP 头部划分为 16 位字
2. 将所有 16 位字相加 (使用一补集加法)
3. 对结果取一补集 (按位翻转)
4. 校验时: 数据 + 校验和 = 0xFFFF (所有位为 1)
```

## 3. UDP 协议 (User Datagram Protocol)

UDP 提供无连接、尽力而为的数据报服务：

```
 0      7 8     15 16    23 24    31
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Source Port          |       Destination Port        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            Length             |           Checksum            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             data                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**特点**:
- 无连接: 不需要预先建立连接
- 不可靠: 不保证送达、不保证顺序、不保证不重复
- 无流控制和拥塞控制
- 适合: DNS, VoIP, 视频流, 游戏 (低延迟优先)

## 4. DNS 协议 (Domain Name System)

DNS 将域名映射为 IP 地址。本质是分层分布式数据库。

### DNS 层次结构

```
. (根域 - Root)
  |
  +-- com (顶级域 - TLD)
  |     |
  |     +-- example (权威域 - Authoritative)
  |           |
  |           +-- www.example.com -> 93.184.216.34
  |           +-- mail.example.com -> 93.184.216.35
  |
  +-- org
  |    ...
  +-- net
       ...
```

### DNS 查询过程

```
1. 客户端 → 本地 DNS (stub resolver)
   "example.com 的 A 记录？"

2. 本地 DNS → 根服务器 (root)
   "com 的 NS 记录？"
   ← a.gtld-servers.net (IP ...)

3. 本地 DNS → TLD 服务器 (.com)
   "example.com 的 NS 记录？"
   ← ns1.example.com (IP ...)

4. 本地 DNS → 权威服务器 (ns1.example.com)
   "example.com 的 A 记录？"
   ← 93.184.216.34

递归查询: 本地DNS代客户端完成全部查询
迭代查询: DNS服务器返回下一跳的指向
```

### DNS 资源记录类型

| 类型 | 编号 | 描述 |
|------|------|------|
| A | 1 | IPv4 地址 |
| AAAA | 28 | IPv6 地址 |
| CNAME | 5 | 别名 (Canonical Name) |
| MX | 15 | 邮件交换服务器 |
| NS | 2 | 域名服务器 |
| SOA | 6 | 权威区域开始 |
| TXT | 16 | 文本记录 (SPF, DKIM) |
| PTR | 12 | 反向查询 (IP→域名) |

### DNS 消息格式

```
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|                      ID                        |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|                    QDCOUNT                     |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|                    ANCOUNT                     |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|                    NSCOUNT                     |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|                    ARCOUNT                     |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
```

## 5. TCP 协议

### TCP 关键特性

1. **面向连接**: 三次握手建立连接
2. **可靠**: 确认/重传确保数据送达
3. **有序**: 序列号确保按序交付
4. **流控制**: 滑动窗口防止发送方淹没接收方
5. **拥塞控制**: 慢启动、拥塞避免、快速恢复
6. **全双工**: 双方可同时发送和接收
7. **字节流**: 没有消息边界，应用层需自行处理边界

### 拥塞控制算法演进

| 算法 | 年份 | 特点 |
|------|------|------|
| TCP Tahoe | 1988 | 慢启动 + 拥塞避免 + 快速重传 |
| TCP Reno | 1990 | +快速恢复 (当前最广泛部署) |
| TCP NewReno | 1996 | 改进快速恢复处理部分 ACK |
| TCP Vegas | 1995 | 基于延迟而非丢包检测拥塞 |
| CUBIC | 2008 | Linux 默认, 三次函数增长 |
| BBR | 2016 | Google, 基于带宽估计 |

## 6. TLS 协议

### TLS 1.3 设计目标

- **机密性**: 加密保护数据不可被窃听
- **完整性**: 检测篡改 (AEAD)
- **认证**: 服务器 (可选的客户端) 身份验证
- **前向保密**: 临时密钥保护历史会话
- **低延迟**: 1-RTT 握手, 0-RTT 恢复

### HTTPS 连接建立 (TLS 握手内嵌于 TCP 中)

```
TCP 三次握手 (1 RTT)
  + TLS 1.3 握手 (1 RTT)
  = 共 2 RTT 建立安全的 HTTP 连接 (首次访问)
```

建立连接后，应用数据通过 AEAD 加密传输。

### 常出现的 TLS 攻击

| 攻击 | TLS 1.3 对策 |
|------|-------------|
| BEAST (2011) | 仅使用 AEAD, 移除 CBC |
| POODLE (2014) | 移除 SSL 3.0, 移除 CBC |
| Logjam (2015) | 最小 DH 组 2048 位, 移除导出密码 |
| FREAK (2015) | 移除所有导出级密码 |
| DROWN (2016) | 禁用 SSLv2 回退 |
| BLEICHENBACHER (1998) | 移除 RSA PKCS#1 v1.5 密钥传输 |
| 降级攻击 | supported_versions 扩展防止降级到 TLS 1.2 |

## 7. HTTP/1.1 协议

### HTTP 请求格式

```
方法 SP URI SP 版本 CRLF
头部字段: 值 CRLF
...
CRLF
[请求体]
```

示例:
```
GET /index.html HTTP/1.1\r\n
Host: www.example.com\r\n
User-Agent: Mozilla/5.0\r\n
Accept: text/html\r\n
Connection: keep-alive\r\n
\r\n
```

### HTTP 响应格式

```
版本 SP 状态码 SP 原因短语 CRLF
头部字段: 值 CRLF
...
CRLF
[响应体]
```

示例:
```
HTTP/1.1 200 OK\r\n
Date: Mon, 18 May 2026 10:00:00 GMT\r\n
Server: Apache/2.4.41\r\n
Content-Type: text/html; charset=UTF-8\r\n
Content-Length: 1270\r\n
Connection: keep-alive\r\n
\r\n
<!DOCTYPE html>...
```

### HTTP 状态码类别

| 范围 | 类别 | 示例 |
|------|------|------|
| 1xx | Informational | 100 Continue, 101 Switching Protocols |
| 2xx | Success | 200 OK, 201 Created, 204 No Content |
| 3xx | Redirection | 301 Moved Permanently, 302 Found, 304 Not Modified |
| 4xx | Client Error | 400 Bad Request, 401 Unauthorized, 403 Forbidden, 404 Not Found |
| 5xx | Server Error | 500 Internal Server Error, 502 Bad Gateway, 503 Service Unavailable |

### 分块传输编码 (Chunked Transfer Encoding)

当服务器无法预先知道响应体大小时，使用分块编码：

```
HTTP/1.1 200 OK\r\n
Transfer-Encoding: chunked\r\n
\r\n
7\r\n
Mozilla\r\n
9\r\n
Developer\r\n
10\r\n
Network\r\n
0\r\n
\r\n
```

每个块: 十六进制长度 + CRLF + 数据 + CRLF
0 表示结束。

## 8. 参考书目

- **TCP/IP Illustrated, Volume 1** — W. Richard Stevens
- **Computer Networking: A Top-Down Approach** — Kurose & Ross
- **Computer Networks: A Systems Approach** — Peterson & Davie
- **UNIX Network Programming, Volume 1** — W. Richard Stevens
- **High Performance Browser Networking** — Ilya Grigorik (在线免费)
- **Bulletproof TLS and PKI** — Ivan Ristic
