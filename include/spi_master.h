/**
 * @file     spi_master.h
 * @brief    SpiMaster —— SPI Master 通用组件（Config 注入，零板级依赖）
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║        永久全局规则（所有组件强制约束，后续不得违反）       ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║ 1. 组件必须用 class 封装（禁止纯 C 函数 / namespace 作 API）║
 * ║ 2. 配置通过构造函数 Config 结构体注入（禁止硬编码）         ║
 * ║ 3. 组件零板级依赖（禁止 include board_config.h / pin）      ║
 * ║ 4. 状态封装在实例里（禁止全局单例 / static 全局变量）       ║
 * ║ 5. 禁止 Arduino.h 冲突宏（RESET→SOFT_RESET / DISPLAY→IO_DISPLAY / ERROR→BAD_CMD）║
 * ║ 6. 兼容旧 API 用 #define CMD_XXX 别名                      ║
 * ║ 7. 组件目录结构：include/ + src/（PlatformIO 自动识别）     ║
 * ║ 8. 板子侧只做组装：main.cpp 实例化 + 传 Config              ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * 使用示例：
 *   SpiMaster aiSpi({.host=SPI2_HOST, .csPin=GPIO_NUM_4, .intPin=GPIO_NUM_3});
 *   aiSpi.init(); aiSpi.ping();
 *
 * 永久全局规则（10 条）：
 *   1-8 见 proto_common/chip_addr.h
 *   9. GPIO 扩展优先：同类 GPIO ≥ 2 个时，优先用类似串行扩展器（如 74HC595/165、PCA9685、CD4021/4094、MCP23017/ADS1115 等），芯片型号+数量+节约引脚数在注释中列出，由用户审核后实施
 *   10. 版本强制：所有组件必须接入 version_manager（version.h + version.txt + 注册宏）
 */
#ifndef SPI_COMM_SPI_MASTER_H
#define SPI_COMM_SPI_MASTER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "proto_frame.h"

class SpiMaster {
public:
    struct Config {
        spi_host_device_t host;       /* 已经初始化好的 SPI 总线 */
        gpio_num_t        csPin;
        gpio_num_t        intPin;     /* 从机 INT → 主控（-1 = 不用）*/
        uint32_t          clockHz   = 10 * 1000 * 1000;
        uint8_t           mode      = 0;
        uint8_t           queueSize = 1;
        int               retryMax  = 3;
        int               replyTimeoutMs = 50;
        const char*       tag       = "SpiMaster";
    };

    explicit SpiMaster(const Config& cfg);
    ~SpiMaster();

    esp_err_t init();
    bool      isInited() const { return dev_ != nullptr; }

    /* 发送并等回复帧（replyOut 可 null 表示不关心）*/
    esp_err_t send(const ProtoFrame& frame, ProtoFrame* replyOut = nullptr);

    /* PING/PONG 健康检查 */
    esp_err_t ping(uint8_t fromAddr, uint8_t toAddr);

    /* 等 INT 拉低（组件内部用，public 方便板子侧灵活调用）*/
    bool waitIntLow(int timeoutMs);

    spi_device_handle_t handle() const { return dev_; }

private:
    Config              cfg_;
    spi_device_handle_t dev_ = nullptr;
    SemaphoreHandle_t   lock_ = nullptr;   /* 🆕 每实例一把 mutex */

    esp_err_t xfer(const uint8_t* txBuf, uint16_t txLen,
                   uint8_t* rxBuf, uint16_t rxLen);
};

#endif /* SPI_COMM_SPI_MASTER_H */
