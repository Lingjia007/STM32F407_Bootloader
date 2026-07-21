# STM32F407 安全 Bootloader

基于 STM32F407VGT6 的工业级安全 Bootloader，涵盖 A/B 双分区无缝升级、AES-256 加密 + Ed25519 签名安全固件包、地址重定位、差分升级、多渠道下载（UART/SD卡/SPI Flash/WiFi OTA）等核心技术。

> 详细设计文档请参考博客文章：[STM32F407 安全 Bootloader 设计：A/B 双分区、加密签名与多渠道升级](https://lingjia007.github.io/Hugo_Web/p/stm32f407-%E5%AE%89%E5%85%A8-bootloader-%E8%AE%BE%E8%AE%A1a/b-%E5%8F%8C%E5%88%86%E5%8C%BA%E5%8A%A0%E5%AF%86%E7%AD%BE%E5%90%8D%E4%B8%8E%E5%A4%9A%E6%B8%A0%E9%81%93%E5%8D%87%E7%BA%A7/)

## 解决的问题

| 问题 | 传统方案 | 本项目方案 |
|---|---|---|
| 升级中断电导致设备变砖 | 单分区直接覆盖，断电即死 | A/B 双分区，升级失败自动回滚 |
| 固件被篡改或伪造 | 无校验或仅 CRC | Ed25519 签名 + HMAC-SHA256 认证 |
| 固件被窃取逆向 | 明文传输 | AES-256-CBC/CTR 加密 + 设备绑定密钥 |
| 旧版本固件降级攻击 | 无防护 | 安全计数器防回滚 |
| 为两个分区维护两套编译配置 | 维护两份工程 | 地址自动重定位 + 双 Target 编译，源码只需一份 |
| 升级包过大占用带宽 | 全量升级 | HPatch + tuz 差分升级 |

## Flash 内存布局

STM32F407VGT6 拥有 1MB 内部 Flash，分区如下：

| 起始地址 | 区域 | 大小 | Flash 扇区 |
|---|---|---|---|
| `0x0800_0000` | Bootloader | 128 KB | Sector 0-4 |
| `0x0802_0000` | Slot A | 384 KB | Sector 5-7 |
| `0x0808_0000` | Slot B | 384 KB | Sector 8-10 |
| `0x080E_0000` | Download Cache | 64 KB | Sector 11 (部分) |
| `0x080F_0000` | Metadata | 64 KB | Sector 11 (部分) |

## 核心特性

### A/B 双分区系统

- **分区状态机**：IDLE → TESTING → CONFIRMED，支持自动回滚
- **追加写入策略**：Metadata 区域追加写入，避免频繁擦除，实例用完时自动压缩
- **启动保护**：TESTING 状态下启动失败超限 3 次自动回滚到另一分区

### 安全固件包

自定义 `.iap.bin` 格式，将加密、签名、校验融为一体：

| 区域 | 大小 | 说明 |
|---|---|---|
| Header | 64 bytes | 魔术字、版本、加密/签名算法等 |
| DynamicSalt | 16 bytes | 随机盐值，每次打包不同 |
| IV | 16 bytes | AES 初始化向量 |
| Ciphertext | N bytes | AES-256-CBC/CTR 加密的固件 |
| Signature | 64 bytes | Ed25519 数字签名 |

### 安全信任链（6 关验证）

1. **HMAC-SHA256** Header 认证
2. **防回滚**与硬件兼容性检查
3. **HKDF** 设备绑定密钥派生（AES_Key = HKDF(Salt, DevKey, UID)）
4. **AES-256** 解密与流式处理
5. **Ed25519** 数字签名验证
6. **SHA-256** Flash 回读完整性校验

### 密钥存储方案

| 密钥 | 存储位置 | 特性 |
|---|---|---|
| DevKey (16B) | STM32 OTP 区域 | 一次性写入，不可修改 |
| Ed25519 公钥 (32B) | 固件中硬编码 | 可公开 |
| UID (12B) | STM32 唯一ID 寄存器 | 出厂固化 |
| AES 密钥 (32B) | 运行时 HKDF 派生 | 不存储，用完即弃 |

### 多渠道固件升级

- **UART YMODEM**：串口下载，支持 128 字节和 1K 字节包
- **SD 卡升级**：FatFS 文件系统读取 `.bin` / `.iap.bin` / `.hdiff`
- **SPI Flash 升级**：W25Q128 + LittleFS，适用于无 SD 卡卡槽的产品
- **WiFi OTA 升级**：ESP8266 + OneNET 平台，MQTT 协议
- **差分升级**：HPatch Lite + tuz 压缩，大幅减少传输数据量

### 平台抽象层：Transport 架构

通过 Source（读取）和 Target（写入）接口抽象，下载逻辑与存储介质完全解耦：

| Transport | Source | Target | 说明 |
|---|---|---|---|
| `g_slot_a_flash` | - | Yes | Slot A 内部 Flash |
| `g_slot_b_flash` | - | Yes | Slot B 内部 Flash（带重定位） |
| `g_download_cache_flash` | - | Yes | 下载缓存区 |
| `g_fatfs_transport` | Yes | Yes | SD 卡 FatFS |
| `g_lfs_transport` | Yes | Yes | SPI Flash LittleFS |

### 地址重定位

写入 Slot B 时，自动对落在 Slot A 地址范围内的绝对地址字进行偏移修正，配合 Keil 双 Target 编译，App 侧只需维护一份源码。

### 交互式菜单系统

通过 UART4 访问的功能丰富菜单，所有功能项通过宏定义控制编译。

## 启动流程

1. 上电/复位
2. 检查 `update_flag == JUMP_FLAG_MAGIC`？→ 直接跳转 App（快速启动）
3. HAL 初始化 → 时钟配置 → 外设初始化
4. A/B 分区状态检查（TESTING 验证 / CONFIRMED 正常启动）
5. 等待 UART 命令（1.5秒超时），收到 'M' 进入菜单
6. 检查 `update_flag == UPDATE_FLAG_MAGIC`？→ 进入菜单（App 请求更新）
7. 自动跳转到活动分区的 App

## 项目结构

```
STM32F407_Bootloader/
├── Bootloader_Core/          # 核心模块
│   ├── bootloader_core.c/h   # Bootloader 下载引擎与跳转逻辑
│   ├── ab_partition.c/h      # A/B 双分区管理
│   └── firmware_package.c/h  # 安全固件包处理引擎
├── Core/                     # STM32 HAL 核心代码
│   ├── Inc/                  # 头文件（GPIO, UART, SPI, SDIO, RNG, RTC 等）
│   └── Src/                  # 源文件（main.c, 中断处理, MSP 等）
├── Platform/                 # 平台抽象层接口
│   ├── Inc/                  # platform_transport.h, platform_flash.h 等
│   └── Src/                  # platform_config.c
├── Impl/                     # 平台抽象层实现（STM32 特化）
│   ├── Inc/                  # 实现头文件
│   └── Src/                  # Flash, UART, FatFS, LittleFS, W25Q128, ESP8266 等实现
├── Service/                  # 业务服务层
│   ├── Inc/                  # AES 解密, Ed25519 验证, HPatch, 菜单, YMODEM, OTA 等
│   └── Src/                  # 对应服务实现
├── IAP/                      # IAP 交互模块
│   ├── menu.c/h              # 交互式菜单系统
│   └── ymodem.c/h            # YMODEM 协议
├── ESP8266_OTA/              # WiFi OTA 模块
│   ├── esp8266_ota_api.c/h   # ESP8266 OTA API 封装
│   └── esp8266_ota_config.h  # OTA 配置
├── LED/                      # LED 指示模块
├── FATFS/                    # FatFS 文件系统（SD 卡）
├── USB_DEVICE/               # USB MSC 设备（通过 USB 传输固件）
├── utils/                    # 加密与工具库
│   ├── aes.c/h               # AES-256 加解密
│   ├── ed25519.c/h           # Ed25519 签名
│   ├── hkdf.c/h              # HKDF 密钥派生
│   ├── sha256.c/h            # SHA-256 哈希
│   ├── sha512.c/h            # SHA-512 哈希
│   ├── hpatch_lite.c/h       # HPatch Lite 差分升级
│   ├── tuz_dec.c/h           # tuz 解压缩
│   ├── cJSON.c/h             # JSON 解析
│   └── ...                   # 其他加密原语（Curve25519, MD5 等）
├── Drivers/                  # STM32 HAL + CMSIS 驱动库
├── Middlewares/              # FatFs + USB Device 中间件
└── MDK-ARM/                  # Keil MDK 工程文件
```

## 开发环境

- **MCU**：STM32F407VGT6 (Cortex-M4, 168MHz, 1MB Flash, 192KB SRAM)
- **IDE**：Keil MDK-ARM (uvprojx)
- **HAL 库**：STM32F4xx HAL Driver
- **调试器**：J-Link
- **外设**：UART4 (菜单), SDIO (SD卡), SPI (W25Q128), ESP8266 (WiFi)

## 使用

1. 使用 Keil MDK 打开 `MDK-ARM/STM32F407VGT6_Bootloader.uvprojx`
2. 编译并下载到 STM32F407VGT6
3. 通过 UART4（波特率 115200）发送 'M' 进入 Bootloader 菜单
4. 在菜单中选择升级方式（UART YMODEM / SD 卡 / SPI Flash / WiFi OTA）

## License

本项目仅供学习参考使用。
