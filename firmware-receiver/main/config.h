#pragma once

#define PIN_LORA_CS       8
#define PIN_LORA_SCK      9
#define PIN_LORA_MOSI    10
#define PIN_LORA_MISO    11
#define PIN_LORA_RST     12
#define PIN_LORA_BUSY    13
#define PIN_LORA_DIO1    14

#define LORA_FREQUENCY_HZ        915000000
#define LORA_BANDWIDTH_KHZ       500
#define LORA_SPREADING_FACTOR    10
#define LORA_CODING_RATE         5       // 5 = coding rate 4/5
#define LORA_SYNC_WORD           0x1424
#define LORA_PREAMBLE_LEN        8

#define LORA_USE_TCXO            1
#define LORA_TCXO_VOLTAGE        0x02    // 1.8 V
#define LORA_USE_DIO2_RF_SWITCH  1
#define LORA_USE_DCDC            1

#define LORA_SPI_HZ              8000000
#define LORA_ANTENNA_CONNECTED   1
#define LORA_MAX_PACKET          255