/**
 * @file     spi_master.cpp
 * @brief    SPI Master 组件实现 —— 零板级依赖
 *
 * 修改：2026-11-21 组件化（Config 注入，去掉 board.h）
 */
#include "spi_master.h"
#include <Arduino.h>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

SpiMaster::SpiMaster(const Config& cfg) : cfg_(cfg) {}

SpiMaster::~SpiMaster()
{
    if (lock_) {
        vSemaphoreDelete(lock_);
        lock_ = nullptr;
    }
}

esp_err_t SpiMaster::init()
{
    if (dev_) return ESP_OK;

    spi_device_interface_config_t devCfg = {};
    devCfg.mode           = cfg_.mode;
    devCfg.clock_speed_hz = cfg_.clockHz;
    devCfg.spics_io_num   = cfg_.csPin;
    devCfg.queue_size     = cfg_.queueSize;

    esp_err_t ret = spi_bus_add_device(cfg_.host, &devCfg, &dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(cfg_.tag, "spi_bus_add_device 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 🆕 创建互斥锁 */
    lock_ = xSemaphoreCreateMutex();
    if (!lock_) {
        ESP_LOGE(cfg_.tag, "创建 mutex 失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(cfg_.tag, "SPI Master 就绪 (host=%d, CS=%d, clock=%luMHz, mutex=%p)",
             cfg_.host, cfg_.csPin, (unsigned long)(cfg_.clockHz / 1000000), (void*)lock_);
    return ESP_OK;
}

esp_err_t SpiMaster::xfer(const uint8_t* txBuf, uint16_t txLen,
                          uint8_t* rxBuf, uint16_t rxLen)
{
    spi_transaction_t t = {};
    t.length    = rxLen * 8;
    t.tx_buffer = txBuf;
    t.rx_buffer = rxBuf;
    t.rxlength  = rxLen * 8;
    return spi_device_transmit(dev_, &t);
}

bool SpiMaster::waitIntLow(int timeoutMs)
{
    if (cfg_.intPin < 0) return true;  /* 没有 INT 引脚就跳过 */

    uint32_t elapsed = 0;
    while (elapsed < (uint32_t)timeoutMs) {
        if (gpio_get_level(cfg_.intPin) == 0) return true;
        vTaskDelay(pdMS_TO_TICKS(2));
        elapsed += 2;
    }
    return false;
}

esp_err_t SpiMaster::send(const ProtoFrame& frame, ProtoFrame* replyOut)
{
    if (!dev_) return ESP_ERR_INVALID_STATE;
    if (!lock_) return ESP_ERR_INVALID_STATE;

    /* 🆕 加锁：同一实例多任务互斥 */
    if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t txBuf[ProtoConst::FRAME_MAX];
    uint16_t txLen = 0;
    if (!frame.encode(txBuf, txLen)) {
        ESP_LOGE(cfg_.tag, "帧编码失败");
        xSemaphoreGive(lock_);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rxBuf[ProtoConst::FRAME_MAX] = {};
    esp_err_t result = ESP_ERR_TIMEOUT;

    for (int attempt = 0; attempt < cfg_.retryMax; attempt++) {
        esp_err_t ret = xfer(txBuf, txLen, rxBuf, txLen);
        if (ret != ESP_OK) continue;

        if (!waitIntLow(cfg_.replyTimeoutMs)) {
            ESP_LOGW(cfg_.tag, "INT 超时，重试 (%d/%d)", attempt + 1, cfg_.retryMax);
            continue;
        }

        memset(rxBuf, 0xFF, sizeof(rxBuf));
        ret = xfer(nullptr, 0, rxBuf, ProtoConst::FRAME_MAX);
        if (ret != ESP_OK) continue;

        ProtoFrame reply;
        if (ProtoFrame::parse(rxBuf, ProtoConst::FRAME_MAX, reply, nullptr)) {
            if (replyOut) *replyOut = reply;
            result = ESP_OK;
            break;
        }
        ESP_LOGW(cfg_.tag, "回复帧解析失败，重试 (%d/%d)", attempt + 1, cfg_.retryMax);
    }

    if (result != ESP_OK) {
        ESP_LOGE(cfg_.tag, "通信超时（%d 次重试）", cfg_.retryMax);
    }

    /* 🆕 解锁 */
    xSemaphoreGive(lock_);
    return result;
}

esp_err_t SpiMaster::ping(uint8_t fromAddr, uint8_t toAddr)
{
    ProtoFrame ping;
    ping.makePing(fromAddr, toAddr);

    ProtoFrame reply;
    esp_err_t ret = send(ping, &reply);
    if (ret != ESP_OK) return ret;

    if (reply.cmd == static_cast<uint8_t>(ChipCmd::PONG)) {
        ESP_LOGI(cfg_.tag, "PONG OK ✅");
        return ESP_OK;
    }

    ESP_LOGW(cfg_.tag, "PING 收到非 PONG (cmd=0x%02X)", reply.cmd);
    return ESP_ERR_NOT_SUPPORTED;
}

#include "spi_comm_version.h"
#include "version_manager.h"
COMPONENT_VERSION_REGISTER(spi_comm, SPI_COMM_VERSION_STRING)
