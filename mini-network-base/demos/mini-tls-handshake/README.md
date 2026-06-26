# Mini TLS 1.3 握手协议深度解析

> 参考 RFC 8446, Stanford CS255, MIT 6.858

## 概述

TLS (Transport Layer Security) 1.3 是互联网上最重要的安全协议，用于保护 HTTP (形成 HTTPS)、电子邮件、即时通讯等应用层协议的通信安全。本文档深入解析 TLS 1.3 的握手过程。

## 1. TLS 在协议栈中的位置

```
+------------------------------------------+
|          Application Layer                |
|      (HTTP, SMTP, IMAP, DNS, etc.)       |
+------------------------------------------+
|                                          |
|          TLS Record Layer                 |  <-- TLS 1.3
|   (Encryption, Authentication, Integrity) |
+------------------------------------------+
|          TCP (Reliable Transport)         |
+------------------------------------------+
|          IP (Network Layer)               |
+------------------------------------------+
```

TLS 本身分为两层：
- **TLS Record Protocol**: 负责对应用数据进行加密和完整性保护
- **TLS Handshake Protocol**: 负责协商加密参数和身份认证

## 2. TLS 1.3 握手流程 (1-RTT)

### 完整握手消息序

```
Client                                            Server

ClientHello
(支持的密码套件, 密钥共享, 随机数)      -------->
                                                  ServerHello
                                                  (选定的密码套件, 密钥共享, 随机数)
                                                  {EncryptedExtensions}
                                                  (加密的扩展参数)
                                                  {Certificate}
                               <--------          (服务端证书链)
                                                  {CertificateVerify}
                                                  (证书签名验证)
                                                  {Finished}
                                                  (握手完整性验证)
{Certificate}*
{CertificateVerify}*
{Finished}                         -------->
                                                  [Application Data]
[Application Data]              <------->        [Application Data]
```

`{}` 表示使用握手流量密钥 (handshake traffic key) 加密的消息
`[]` 表示使用应用数据密钥 (application data key) 加密的消息
`*` 表示客户端证书是可选的 (仅在需要双向认证时发送)

### 详细步骤

#### Step 1: ClientHello

客户端生成并发送：
- **client_random**: 32 字节随机数
- **legacy_session_id**: 为了兼容 TLS 1.2 中间件而保留 (TLS 1.3 不使用会话 ID)
- **cipher_suites**: 支持的对称加密套件列表
- **supported_groups**: 支持的椭圆曲线组 (如 x25519, secp256r1)
- **key_share**: 客户端 ECDHE 公钥 (可包含多组)
- **signature_algorithms**: 支持的签名算法
- **supported_versions**: 必选扩展，列出支持的 TLS 版本
- **psk_key_exchange_modes**: PSK 模式 (PSK-only / PSK+DHE)
- **pre_shared_key**: 可选的 PSK 列表 (用于 0-RTT 或会话恢复)

```
ClientHello {
    legacy_version: 0x0303  // TLS 1.2 兼容
    random: 32 bytes
    legacy_session_id: <0..32>
    cipher_suites: [TLS_AES_128_GCM_SHA256, ...]
    legacy_compression_methods: [0x00]
    extensions: {
        supported_versions: TLS 1.3
        supported_groups: x25519, secp256r1
        key_share: {
            x25519: <client_public_key>
        }
        signature_algorithms: ecdsa_secp256r1_sha256, ...
        server_name: "example.com"  // SNI
    }
}
```

#### Step 2: ServerHello

服务器选择参数并生成：
- **server_random**: 32 字节随机数
- **cipher_suite**: 选定的单个密码套件
- **key_share**: 服务器 ECDHE 公钥

```
ServerHello {
    legacy_version: 0x0303  // TLS 1.2 兼容
    random: 32 bytes
    legacy_session_id_echo: <server_generated>
    cipher_suite: TLS_AES_128_GCM_SHA256
    legacy_compression_method: 0x00
    extensions: {
        supported_versions: TLS 1.3
        key_share: {
            x25519: <server_public_key>
        }
    }
}
```

**此时密钥派生**：
双方使用 DH 密钥交换算法 (ECDHE) 计算共享秘密。

## 3. ECDHE 密钥交换 (Elliptic Curve Diffie-Hellman Ephemeral)

### 数学原理

ECDHE 基于椭圆曲线上的离散对数问题：

```
双方协商椭圆曲线参数: G (基点), n (阶)

Client:
  生成私钥: a (随机 256-bit 整数)
  计算公钥: A = a * G
  发送: A

Server:
  生成私钥: b (随机 256-bit 整数)
  计算公钥: B = b * G
  发送: B

双方分别计算共享秘密:
  Client: S = a * B = a * (b * G) = (a*b) * G
  Server: S = b * A = b * (a * G) = (a*b) * G

结果: 相同的共享秘密 S (通常取 x 坐标)
```

**Ephemeral** 表示每次会话生成新的临时密钥对，实现**前向保密**(Forward Secrecy)。

### x25519 (Curve25519)

TLS 1.3 中**强制必须支持**的曲线：
- 基于蒙哥马利形式: `y² = x³ + 486662x² + x mod (2^255 - 19)`
- 私钥 32 字节，公钥 32 字节
- 高效的恒定时间实现 (定时攻击安全)
- 由 Daniel J. Bernstein 设计

### 密钥派生 (Key Schedule)

TLS 1.3 使用 HKDF (HMAC-based Key Derivation Function) 进行密钥派生：

```
                            PSK ->  HKDF-Extract = Early Secret
                                          |
                                          +-> Derive-Secret(., "ext binder" | "res binder", "")
                                          |   = binder_key
                                          |
                                 (EC)DHE -> HKDF-Extract = Handshake Secret
                                          |
                                          +-> Derive-Secret(., "c hs traffic", ClientHello...ServerHello)
                                          |   = client_handshake_traffic_secret
                                          |
                                          +-> Derive-Secret(., "s hs traffic", ClientHello...ServerHello)
                                          |   = server_handshake_traffic_secret
                                          |
                           0 -> HKDF-Extract = Master Secret
                                          |
                                          +-> Derive-Secret(., "c ap traffic", ClientHello...ServerFinished)
                                          |   = client_application_traffic_secret_0
                                          |
                                          +-> Derive-Secret(., "s ap traffic", ClientHello...ServerFinished)
                                          |   = server_application_traffic_secret_0
                                          |
                                          +-> Derive-Secret(., "exp master", ClientHello...ServerFinished)
                                          |   = exporter_master_secret
                                          |
                                          +-> Derive-Secret(., "res master", ClientHello...ClientFinished)
                                              = resumption_master_secret
```

#### Finished 验证数据推导

```
finished_key = HKDF-Expand-Label(server_handshake_traffic_secret,
                                  "finished", "", Hash.length)
verify_data = HMAC(finished_key, Transcript-Hash(Handshake Context))
```

`Transcript-Hash` 是迄今为止所有握手消息的哈希链：
```
Transcript-Hash(M1, M2, ... Mn) = Hash(M1 || M2 || ... || Mn)
```

## 4. 证书与认证

### X.509 证书链

```
Root CA (自签名)
  |
  v
Intermediate CA
  |
  v
Server Certificate (example.com)
```

验证步骤：
1. **证书链验证**: 逐级验证签名直到信任锚(根证书)
2. **有效期检查**: 证书 notBefore ≤ 当前时间 ≤ notAfter
3. **域名匹配**: 证书 SAN (Subject Alternative Name) 或 CN 包含访问的域名
4. **吊销检查**: OCSP Stapling 或 CRL 查询
5. **密钥用途**: 检查 Key Usage 和 Extended Key Usage 扩展

### CertificateVerify

服务器使用证书对应的私钥对 Transcript-Hash(Handshake Context) 签名：

```
CertificateVerify {
    SignatureScheme: ecdsa_secp256r1_sha256
    Signature: sign(server_private_key,
                    "TLS 1.3, server CertificateVerify" ||
                    64个0x20字节 ||
                    Transcript-Hash(Handshake Context))
}
```

客户端验证签名的有效性，确认服务器确实持有证书对应的私钥。

## 5. TLS 1.3 密码套件

| 密码套件 | 值 | AEAD | 哈希 |
|---------|---|------|------|
| TLS_AES_128_GCM_SHA256 | 0x1301 | AES-128-GCM | SHA-256 |
| TLS_AES_256_GCM_SHA384 | 0x1302 | AES-256-GCM | SHA-384 |
| TLS_CHACHA20_POLY1305_SHA256 | 0x1303 | ChaCha20-Poly1305 | SHA-256 |
| TLS_AES_128_CCM_SHA256 | 0x1304 | AES-128-CCM | SHA-256 |

### AEAD (Authenticated Encryption with Associated Data)

TLS 1.3 所有密码套件均为 AEAD 模式：
- **加密** (Encryption): 确保机密性
- **认证** (Authentication): 确保完整性和真实性
- **关联数据** (Associated Data): 对未加密但需认证的数据提供保护

```
AEAD 加密 = (加密, 认证标签)
输入: key, nonce, plaintext, aad
输出: ciphertext || authentication_tag (通常 16 字节)

附加认证数据 (AAD) = 
  ContentType (1 byte) ||
  LegacyRecordVersion (2 bytes) ||
  EpochAndSequenceNumber (8 bytes) ||
  FragmentLength (2 bytes)
```

## 6. 前向保密 (Forward Secrecy)

**定义**: 即使长期私钥(服务器证书私钥)在未来被泄露，过去的会话也不能被解密。

### 实现方式

- 使用临时密钥对 (Ephemeral Key Exchange)：每次会话生成新的 ECDH 密钥对
- 会话结束后，临时私钥被销毁
- 共享秘密仅存于内存中，不写入持久化存储

### 对比

| 方案 | 前向保密 | 说明 |
|------|---------|------|
| RSA 密钥传输 | 否 | 用服务器公钥加密会话密钥，私钥泄露则所有历史会话可解密 |
| 静态 DH | 否 | 长期固定 DH 密钥对 |
| ECDHE | 是 | 每次会话临时 DH 密钥对 |
| DHE | 是 | 有限域上的临时 DH |

### 后向安全 (Post-Compromise Security)

某些协议(如 Signal 的双棘轮)提供更强的属性：即使当前状态泄露，未来消息也不受影响。TLS 1.3 1-RTT 握手不具备此属性。

## 7. 0-RTT 模式

TLS 1.3 支持 0-RTT (Zero Round Trip Time) 数据发送，允许客户端在握手的第一个往返中发送应用数据。

### 前提

客户端需持有有效的 PSK (Pre-Shared Key)，通常来自：
- 之前会话的 `resumption_master_secret` (会话恢复)
- 带外配置的 PSK

### 工作流程

```
Client                                                Server

ClientHello
  + key_share
  + pre_shared_key
  + early_data                    -------->
  + (early Application Data*)               ServerHello
                                       + pre_shared_key
                                       {EncryptedExtensions}
                                       {Finished}
                            <--------        [Application Data*]
(EndOfEarlyData)               -------->
{Finished}                     -------->
[Application Data]          <------->    [Application Data]

* 表示可选的或不依赖于握手数据的消息
```

### 0-RTT 的安全考虑

0-RTT 数据**不提供前向保密**，且存在**重放攻击**风险：
- 攻击者拦截 ClientHello + 0-RTT 数据后，可以重放到服务器
- 缓解措施: 
  - 限制 0-RTT 数据仅用于幂等操作 (如 GET)
  - 记录 ClientHello 的随机数，拒绝重复的 ClientHello
  - 使用 ticket age 和 obfuscated_ticket_age

## 8. 与 TLS 1.2 的主要差异

| 特性 | TLS 1.2 | TLS 1.3 |
|------|---------|---------|
| 握手往返 | 2-RTT | 1-RTT (0-RTT 可复原) |
| 密码套件 | 含密钥交换、认证、加密、哈希 | 仅含 AEAD + 哈希 |
| 密钥交换 | 可选 RSA, DHE, ECDHE | 仅 (EC)DHE |
| 对称加密 | CBC, GCM, ... | 仅 AEAD |
| 签名算法 | 在密码套件中 | 独立的扩展协商 |
| 证书压缩 | 不支持 | 支持 (RFC 8879) |
| SNI 加密 | 明文 | 支持 ESNI/ECH |
| 中间件兼容 | - | 伪装成 TLS 1.2 Session ID |

## 9. 关键参考

- **RFC 8446**: The Transport Layer Security (TLS) Protocol Version 1.3
- **RFC 7748**: Elliptic Curves for Security (Curve25519)
- **RFC 5869**: HMAC-based Extract-and-Expand Key Derivation Function (HKDF)
- **RFC 5280**: Internet X.509 Public Key Infrastructure Certificate and CRL Profile
- **RFC 5116**: An Interface and Algorithms for Authenticated Encryption
- **RFC 8447**: IANA Registry Updates for TLS
- **MIT 6.858**: Computer Systems Security (Lectures 10-12)
- **Stanford CS255**: Introduction to Cryptography
- **The Illustrated TLS 1.3 Connection**: https://tls13.ulfheim.net/

## 10. 本实现局限

本迷你 TLS 1.3 实现的模拟版本仅用于教学演示：

1. **无真实加密操作**: 密钥交换、签名、哈希均为模拟随机数
2. **无证书验证**: 证书仅为示例字符串，不进行实际 X.509 验证
3. **无完整密钥调度**: 未实现完整的 HKDF 派生链
4. **无 AEAD 加密**: 消息未经过真实 AES-GCM 加密
5. **无 0-RTT**: 未实现早期数据和 PSK 模式
6. **简化错误处理**: 未完整处理各种 TLS Alert 协议消息
7. **无会话恢复**: 未实现会话票据(session ticket)机制

建议进一步学习时参考 mbedTLS、OpenSSL 或 wolfSSL 等开源实现的 TLS 握手模块。
