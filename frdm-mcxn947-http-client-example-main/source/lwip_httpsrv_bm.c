/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#include "app.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"

/* ADC */
#include "fsl_lpadc.h"
#include "fsl_spc.h"

/* LwIP */
#include "lwip/dhcp.h"
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/sys.h"
#include "netif/ethernet.h"
#include "ethernetif.h"

/* Ethernet / PHY */
#include "fsl_enet.h"
#include "fsl_phy.h"
#include "fsl_phylan8741.h"
#include "fsl_silicon_id.h"

/* HTTP Client */
#include "http_client.h"

/* =============================================================================
 * CONFIGURARE RETEA
 * ============================================================================= */

/* IP-ul placii FRDM-MCXN947 */
//#define BOARD_IP_1      10
//#define BOARD_IP_2      14
//#define BOARD_IP_3      10
//#define BOARD_IP_4      62

/* Netmask retea */
#define NETMASK_1       255
#define NETMASK_2       255
#define NETMASK_3       254
#define NETMASK_4       0

/* Gateway retea */
#define GATEWAY_1       10
#define GATEWAY_2       14
#define GATEWAY_3       10
#define GATEWAY_4       1

/* IP laptop / server ASP.NET */
#define SERVER_IP_1     10
#define SERVER_IP_2     14
#define SERVER_IP_3     10
#define SERVER_IP_4     99

#define SERVER_PORT     5078
#define SERVER_PATH     "/api/data"

#define DEVICE_KEY      "FRDM-SECRET-2026"
#define DEVICE_LAT_X100000   4579300L
#define DEVICE_LON_X100000   2415200L

/* Intervale */
#define SEND_INTERVAL_MS             5000U
#define RETRY_INTERVAL_MS            1000U
#define LINK_CHECK_INTERVAL_MS       1000U

/* 720 citiri x 5 secunde = aproximativ 1 ora de buffer offline */
#define OFFLINE_BUFFER_CAPACITY      720U
#define OFFLINE_PAYLOAD_SIZE         256U

/* Filtrare temperatura */
#define TEMP_SAMPLE_COUNT            9U
#define TEMP_MAX_JUMP_C              5.0f
#define TEMP_MIN_VALID_C            -50.0f
#define TEMP_MAX_VALID_C             80.0f

/* =============================================================================
 * CONFIGURARE PHY / ENET
 * ============================================================================= */

#ifndef EXAMPLE_PHY_ADDRESS
#define EXAMPLE_PHY_ADDRESS BOARD_ENET0_PHY_ADDRESS
#endif

#ifndef EXAMPLE_PHY_OPS
#define EXAMPLE_PHY_OPS &phylan8741_ops
#endif

#ifndef EXAMPLE_NETIF_INIT_FN
#define EXAMPLE_NETIF_INIT_FN ethernetif0_init
#endif

#ifndef EXAMPLE_CLOCK_FREQ
#define EXAMPLE_CLOCK_FREQ CLOCK_GetFreq(kCLOCK_BusClk)
#endif

#ifndef EXAMPLE_ENET
#define EXAMPLE_ENET ENET0
#endif

/* =============================================================================
 * CONFIGURARE ADC / SENZOR TEMPERATURA KY-028
 * ============================================================================= */

#define ADC_CHANNEL_NUMBER          0U
#define ADC_COMMAND_ID              1U
#define ADC_TRIGGER_ID              0U
#define ADC_VREF_MV                 3300U
#define ADC_12BIT_MAX               4095U

#define NTC_NOMINAL_RESISTANCE      10000.0f
#define NTC_NOMINAL_TEMP            25.0f
#define NTC_BETA                    3950.0f
#define NTC_SERIES_RESISTOR         7200.0f
#define KELVIN_OFFSET               273.15f

/* =============================================================================
 * TIPURI SI VARIABILE GLOBALE
 * ============================================================================= */

typedef struct
{
    float temperatureC;
    uint32_t rawValue;
    uint32_t millivolts;
    uint32_t sampleTimeMs;
    uint8_t retryCount;
} offline_sample_t;

static phy_handle_t phyHandle;

static volatile bool g_http_busy = false;
static volatile bool g_ethernet_link_up = false;
static volatile uint32_t g_success_count = 0;
static volatile uint32_t g_error_count = 0;

/* Buffer circular offline in RAM.
 * Se stocheaza citirea reala si momentul relativ al citirii, nu JSON-ul.
 * JSON-ul este construit abia la trimitere, ca sa putem calcula corect AgeMs.
 */
static offline_sample_t g_offline_buffer[OFFLINE_BUFFER_CAPACITY];
static uint32_t g_buffer_head = 0U;
static uint32_t g_buffer_count = 0U;

/* Esantionul aflat momentan in trimitere HTTP. */
static offline_sample_t g_active_sample;
static bool g_active_sample_valid = false;

static char g_active_payload[OFFLINE_PAYLOAD_SIZE];

/* Forward declarations */
static void http_client_demo_result(void *arg, int status_code, const char *body, u16_t body_len);
static uint32_t ADC_ReadRaw(void);
static float ConvertRawToTemperature(uint32_t rawValue);

/* =============================================================================
 * UTILITARE AFISARE / FORMAT JSON
 * ============================================================================= */

static void PrintTemperature(float temperatureC)
{
    int temp_x10 = (int)(temperatureC * 10.0f);

    if (temp_x10 < 0)
    {
        int abs_temp_x10 = -temp_x10;

        PRINTF("Temperatura calculata: -%d.%d C\r\n",
               abs_temp_x10 / 10,
               abs_temp_x10 % 10);
    }
    else
    {
        PRINTF("Temperatura calculata: %d.%d C\r\n",
               temp_x10 / 10,
               temp_x10 % 10);
    }
}

static int BuildTemperaturePayload(char *outPayload,
                                   uint32_t outSize,
                                   float temperatureC,
                                   uint32_t ageMs)
{
    if ((outPayload == NULL) || (outSize == 0U))
    {
        return -1;
    }

    int temp_x10 = (int)(temperatureC * 10.0f);
    long lat_abs = DEVICE_LAT_X100000 >= 0 ? DEVICE_LAT_X100000 : -DEVICE_LAT_X100000;
    long lon_abs = DEVICE_LON_X100000 >= 0 ? DEVICE_LON_X100000 : -DEVICE_LON_X100000;

    if (temp_x10 < 0)
    {
        int abs_temp_x10 = -temp_x10;

        return snprintf(outPayload,
                        outSize,
                        "{\"DeviceKey\":\"%s\",\"DeviceId\":\"FRDM-MCXN947-01\",\"Temperature\":-%d.%d,\"Latitude\":%s%ld.%05ld,\"Longitude\":%s%ld.%05ld,\"AgeMs\":%lu}",
                        DEVICE_KEY,
                        abs_temp_x10 / 10,
                        abs_temp_x10 % 10,
                        DEVICE_LAT_X100000 < 0 ? "-" : "",
                        lat_abs / 100000L,
                        lat_abs % 100000L,
                        DEVICE_LON_X100000 < 0 ? "-" : "",
                        lon_abs / 100000L,
                        lon_abs % 100000L,
                        (unsigned long)ageMs);
    }

    return snprintf(outPayload,
                    outSize,
                    "{\"DeviceKey\":\"%s\",\"DeviceId\":\"FRDM-MCXN947-01\",\"Temperature\":%d.%d,\"Latitude\":%s%ld.%05ld,\"Longitude\":%s%ld.%05ld,\"AgeMs\":%lu}",
                    DEVICE_KEY,
                    temp_x10 / 10,
                    temp_x10 % 10,
                    DEVICE_LAT_X100000 < 0 ? "-" : "",
                    lat_abs / 100000L,
                    lat_abs % 100000L,
                    DEVICE_LON_X100000 < 0 ? "-" : "",
                    lon_abs / 100000L,
                    lon_abs % 100000L,
                    (unsigned long)ageMs);
}

/* =============================================================================
 * BUFFER OFFLINE
 * ============================================================================= */

static uint32_t OfflineBuffer_Count(void)
{
    return g_buffer_count;
}

static bool OfflineBuffer_IsFull(void)
{
    return g_buffer_count >= OFFLINE_BUFFER_CAPACITY;
}

static bool OfflineBuffer_IsEmpty(void)
{
    return g_buffer_count == 0U;
}

static void OfflineBuffer_PushBack(offline_sample_t sample)
{
    if (OfflineBuffer_IsFull())
    {
        g_buffer_head = (g_buffer_head + 1U) % OFFLINE_BUFFER_CAPACITY;
        g_buffer_count--;
        PRINTF("Buffer offline plin. Cea mai veche citire a fost eliminata.\r\n");
    }

    uint32_t index = (g_buffer_head + g_buffer_count) % OFFLINE_BUFFER_CAPACITY;
    g_offline_buffer[index] = sample;
    g_buffer_count++;
}

static void OfflineBuffer_PushFront(offline_sample_t sample)
{
    if (OfflineBuffer_IsFull())
    {
        g_buffer_count--;
        PRINTF("Buffer offline plin. Ultima citire a fost eliminata pentru retry.\r\n");
    }

    if (g_buffer_head == 0U)
    {
        g_buffer_head = OFFLINE_BUFFER_CAPACITY - 1U;
    }
    else
    {
        g_buffer_head--;
    }

    g_offline_buffer[g_buffer_head] = sample;
    g_buffer_count++;
}

static bool OfflineBuffer_PopFront(offline_sample_t *outSample)
{
    if (outSample == NULL)
    {
        return false;
    }

    if (OfflineBuffer_IsEmpty())
    {
        return false;
    }

    *outSample = g_offline_buffer[g_buffer_head];

    g_buffer_head = (g_buffer_head + 1U) % OFFLINE_BUFFER_CAPACITY;
    g_buffer_count--;

    return true;
}

/* =============================================================================
 * HTTP / NETWORK CALLBACKS
 * ============================================================================= */

static void SendNextBufferedPayload(void)
{
    if (g_http_busy)
    {
        return;
    }

    if (OfflineBuffer_IsEmpty())
    {
        return;
    }

    if (!OfflineBuffer_PopFront(&g_active_sample))
    {
        return;
    }

    g_active_sample_valid = true;

    uint32_t nowMs = sys_now();
    uint32_t ageMs = nowMs - g_active_sample.sampleTimeMs;

    int payloadLen = BuildTemperaturePayload(g_active_payload,
                                             sizeof(g_active_payload),
                                             g_active_sample.temperatureC,
                                             ageMs);

    if ((payloadLen <= 0) || (payloadLen >= (int)sizeof(g_active_payload)))
    {
        PRINTF("Eroare: payload JSON prea mare sau invalid.\r\n");

        if (g_active_sample_valid)
        {
            OfflineBuffer_PushFront(g_active_sample);
            g_active_sample_valid = false;
        }

        return;
    }

    ip_addr_t server_ip;
    IP_ADDR4(&server_ip, SERVER_IP_1, SERVER_IP_2, SERVER_IP_3, SERVER_IP_4);

    PRINTF("\r\nTrimitere POST catre http://%d.%d.%d.%d:%d%s\r\n",
           SERVER_IP_1,
           SERVER_IP_2,
           SERVER_IP_3,
           SERVER_IP_4,
           SERVER_PORT,
           SERVER_PATH);

    PRINTF("Payload trimis: %s\r\n", g_active_payload);
    PRINTF("Content-Length: %d\r\n", payloadLen);
    PRINTF("AgeMs: %lu\r\n", (unsigned long)ageMs);
    PRINTF("Buffer ramas dupa extragere: %lu\r\n", (unsigned long)OfflineBuffer_Count());

    g_http_busy = true;

    err_t err = http_client_post(&server_ip,
                                 SERVER_PORT,
                                 SERVER_PATH,
                                 "application/json",
                                 g_active_payload,
                                 (u16_t)payloadLen,
                                 http_client_demo_result,
                                 NULL);

    if (err != ERR_OK)
    {
        PRINTF("Eroare la pornirea cererii POST: %d\r\n", (int)err);

        if (g_active_sample_valid)
        {
            OfflineBuffer_PushFront(g_active_sample);
            g_active_sample_valid = false;
        }

        g_http_busy = false;
    }
}

void SysTick_Handler(void)
{
    time_isr();
}

static void http_client_demo_result(void *arg, int status_code, const char *body, u16_t body_len)
{
    (void)arg;

    PRINTF("\r\n--- RASPUNS SERVER ---\r\n");
    PRINTF("Status code: %d\r\n", status_code);

    if (status_code >= 200 && status_code < 300)
    {
        g_success_count++;
        PRINTF("Trimitere reusita. Esantion confirmat de server.\r\n");
        g_active_sample_valid = false;
    }
    else
    {
        g_error_count++;

        PRINTF("Eroare HTTP/TCP. Se aplica politica de retry 3 incercari.\r\n");
        PRINTF("Verifica: IP server, port, firewall Windows si daca serverul ruleaza pe 0.0.0.0.\r\n");

        if (g_active_sample_valid)
        {
            if (g_active_sample.retryCount < 3U)
            {
                g_active_sample.retryCount++;
                PRINTF("Retry %u / 3 pentru esantion.\r\n", (unsigned int)g_active_sample.retryCount);
                OfflineBuffer_PushFront(g_active_sample);
            }
            else
            {
                PRINTF("Esantionul a esuat dupa 3 incercari. Ramane salvat in buffer offline pentru retrimitere ulterioara.\r\n");
                OfflineBuffer_PushBack(g_active_sample);
            }

            g_active_sample_valid = false;
        }
    }

    if (body != NULL && body_len > 0)
    {
        PRINTF("Body: ");
        for (u16_t i = 0; i < body_len; i++)
        {
            PRINTF("%c", body[i]);
        }
        PRINTF("\r\n");
    }

    PRINTF("Trimiteri OK: %lu | Erori: %lu | In buffer: %lu\r\n",
           (unsigned long)g_success_count,
           (unsigned long)g_error_count,
           (unsigned long)OfflineBuffer_Count());

    PRINTF("----------------------\r\n");

    g_http_busy = false;
}

/* =============================================================================
 * ADC FUNCTIONS
 * ============================================================================= */

static void ADC_Init_Custom(void)
{
    lpadc_config_t adcConfig;
    lpadc_conv_command_config_t cmdConfig;
    lpadc_conv_trigger_config_t trigConfig;

    SPC_EnableActiveModeAnalogModules(SPC0, kSPC_controlVref);

    CLOCK_AttachClk(kFRO_HF_to_ADC0);
    CLOCK_SetClkDiv(kCLOCK_DivAdc0Clk, 1U);
    CLOCK_EnableClock(kCLOCK_Adc0);

    LPADC_GetDefaultConfig(&adcConfig);
    adcConfig.referenceVoltageSource = kLPADC_ReferenceVoltageAlt2;

    LPADC_Init(ADC0, &adcConfig);
    LPADC_DoAutoCalibration(ADC0);

    LPADC_GetDefaultConvCommandConfig(&cmdConfig);
    cmdConfig.channelNumber = ADC_CHANNEL_NUMBER;
    cmdConfig.sampleChannelMode = kLPADC_SampleChannelSingleEndSideA;
    cmdConfig.conversionResolutionMode = kLPADC_ConversionResolutionStandard;

    LPADC_SetConvCommandConfig(ADC0, ADC_COMMAND_ID, &cmdConfig);

    LPADC_GetDefaultConvTriggerConfig(&trigConfig);
    trigConfig.targetCommandId = ADC_COMMAND_ID;
    trigConfig.enableHardwareTrigger = false;

    LPADC_SetConvTriggerConfig(ADC0, ADC_TRIGGER_ID, &trigConfig);
}

static uint32_t ADC_ReadRaw(void)
{
    lpadc_conv_result_t result;

    LPADC_DoSoftwareTrigger(ADC0, 1U);

    while (!LPADC_GetConvResult(ADC0, &result, 0U))
    {
    }

    return result.convValue >> 3U;
}

static float ConvertRawToTemperature(uint32_t rawValue)
{
    if (rawValue == 0U || rawValue >= ADC_12BIT_MAX)
    {
        return 0.0f;
    }

    /* KY-028:
     * Semnalul analogic este inversat. La temperatura mai mare, tensiunea AO scade.
     * Formula pentru montajul folosit:
     * R_ntc = R_fixed * ADC / (ADC_MAX - ADC)
     */
    float resistance = NTC_SERIES_RESISTOR *
                       (float)rawValue /
                       (float)(ADC_12BIT_MAX - rawValue);

    float steinhart = logf(resistance / NTC_NOMINAL_RESISTANCE) / NTC_BETA;
    steinhart += 1.0f / (NTC_NOMINAL_TEMP + KELVIN_OFFSET);

    return (1.0f / steinhart) - KELVIN_OFFSET;
}

static bool IsTemperatureValid(float temperatureC)
{
    return (temperatureC >= TEMP_MIN_VALID_C) && (temperatureC <= TEMP_MAX_VALID_C);
}

static void SortFloatArray(float *values, uint32_t count)
{
    for (uint32_t i = 0U; i < count; i++)
    {
        for (uint32_t j = i + 1U; j < count; j++)
        {
            if (values[j] < values[i])
            {
                float tmp = values[i];
                values[i] = values[j];
                values[j] = tmp;
            }
        }
    }
}

/* Citeste 9 valori, elimina 2 extreme mici si 2 extreme mari, apoi mediaza cele 5 valori centrale. */
static bool ReadFilteredTemperature(float *outTemperatureC, uint32_t *outRawValue, uint32_t *outMillivolts)
{
    if (outTemperatureC == NULL)
    {
        return false;
    }

    float temperatures[TEMP_SAMPLE_COUNT];
    uint32_t rawSum = 0U;

    for (uint32_t i = 0U; i < TEMP_SAMPLE_COUNT; i++)
    {
        uint32_t raw = ADC_ReadRaw();
        rawSum += raw;
        temperatures[i] = ConvertRawToTemperature(raw);

        volatile uint32_t smallDelay = 20000U;
        while (smallDelay--)
        {
            __asm volatile ("nop");
        }
    }

    SortFloatArray(temperatures, TEMP_SAMPLE_COUNT);

    float sum = 0.0f;
    uint32_t used = 0U;

    for (uint32_t i = 2U; i < (TEMP_SAMPLE_COUNT - 2U); i++)
    {
        if (IsTemperatureValid(temperatures[i]))
        {
            sum += temperatures[i];
            used++;
        }
    }

    if (used == 0U)
    {
        return false;
    }

    uint32_t avgRaw = rawSum / TEMP_SAMPLE_COUNT;

    *outTemperatureC = sum / (float)used;

    if (outRawValue != NULL)
    {
        *outRawValue = avgRaw;
    }

    if (outMillivolts != NULL)
    {
        *outMillivolts = (avgRaw * ADC_VREF_MV) / ADC_12BIT_MAX;
    }

    return true;
}

/* Limiteaza salturile bruste de temperatura.
 * Nu respinge citirea, ci o apropie treptat de valoarea masurata.
 *
 * Exemplu:
 * ultima valoare valida = 20 C
 * noua masurare = 10 C
 * TEMP_MAX_JUMP_C = 5 C
 *
 * rezultat 1: 15 C
 * rezultat 2: 10 C, daca masurarea ramane 10 C
 */
static bool ApplyTemperatureAnomalyFilter(float measuredTemperatureC, float *acceptedTemperatureC)
{
    static bool hasLastValidTemperature = false;
    static float lastValidTemperatureC = 0.0f;

    if (acceptedTemperatureC == NULL)
    {
        return false;
    }

    if (!IsTemperatureValid(measuredTemperatureC))
    {
        PRINTF("Citire invalida fizic. Nu se accepta.\r\n");
        return false;
    }

    if (!hasLastValidTemperature)
    {
        lastValidTemperatureC = measuredTemperatureC;
        hasLastValidTemperature = true;
        *acceptedTemperatureC = measuredTemperatureC;
        return true;
    }

    float diff = measuredTemperatureC - lastValidTemperatureC;

    if (diff > TEMP_MAX_JUMP_C)
    {
        lastValidTemperatureC = lastValidTemperatureC + TEMP_MAX_JUMP_C;

        PRINTF("Salt pozitiv limitat la %d grade C.\r\n", (int)TEMP_MAX_JUMP_C);
        PRINTF("Temperatura masurata: ");
        PrintTemperature(measuredTemperatureC);
        PRINTF("Temperatura acceptata gradual: ");
        PrintTemperature(lastValidTemperatureC);

        *acceptedTemperatureC = lastValidTemperatureC;
        return true;
    }

    if (diff < -TEMP_MAX_JUMP_C)
    {
        lastValidTemperatureC = lastValidTemperatureC - TEMP_MAX_JUMP_C;

        PRINTF("Salt negativ limitat la %d grade C.\r\n", (int)TEMP_MAX_JUMP_C);
        PRINTF("Temperatura masurata: ");
        PrintTemperature(measuredTemperatureC);
        PRINTF("Temperatura acceptata gradual: ");
        PrintTemperature(lastValidTemperatureC);

        *acceptedTemperatureC = lastValidTemperatureC;
        return true;
    }

    /* Daca diferenta este sub prag, acceptam valoarea masurata.
     * Optional, pastram o usoara netezire ca sa nu tremure temperatura.
     */
    lastValidTemperatureC = measuredTemperatureC;
    *acceptedTemperatureC = measuredTemperatureC;

    return true;
}

/* =============================================================================
 * NETWORK DHCP HELPERS
 * ============================================================================= */

static bool Network_HasIp(struct netif *netif)
{
    return !ip4_addr_isany_val(*netif_ip4_addr(netif));
}

static bool Network_StartDhcpAndWait(struct netif *netif, uint32_t timeoutMs)
{
    PRINTF("Pornesc DHCP...\r\n");

    dhcp_stop(netif);
    dhcp_start(netif);

    uint32_t startMs = sys_now();

    while (!Network_HasIp(netif))
    {
        ethernetif_input(netif);
        sys_check_timeouts();

        if ((uint32_t)(sys_now() - startMs) > timeoutMs)
        {
            PRINTF("DHCP timeout. Placa nu a primit IP.\r\n");
            return false;
        }
    }

    PRINTF("IP placa DHCP: %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif)));
    PRINTF("Gateway DHCP: %s\r\n", ip4addr_ntoa(netif_ip4_gw(netif)));
    PRINTF("Netmask DHCP: %s\r\n", ip4addr_ntoa(netif_ip4_netmask(netif)));

    return true;
}

/* =============================================================================
 * MAIN
 * ============================================================================= */

int main(void)
{
    struct netif netif;

    ip4_addr_t netif_ipaddr;
    ip4_addr_t netif_netmask;
    ip4_addr_t netif_gw;

    ethernetif_config_t enet_config = {
        .phyHandle   = &phyHandle,
        .phyAddr     = EXAMPLE_PHY_ADDRESS,
        .phyOps      = EXAMPLE_PHY_OPS,
        .phyResource = EXAMPLE_PHY_RESOURCE,
    };

    BOARD_InitHardware();
    time_init();

    PRINTF("\r\n=====================================\r\n");
    PRINTF("FRDM-MCXN947 - IoT HTTP Client\r\n");
    PRINTF("=====================================\r\n");

    PRINTF("Initializare ADC...\r\n");
    ADC_Init_Custom();
    PRINTF("ADC initializat.\r\n");

    (void)SILICONID_ConvertToMacAddr(&enet_config.macAddress);
    enet_config.srcClockHz = EXAMPLE_CLOCK_FREQ;

    IP4_ADDR(&netif_ipaddr, 0, 0, 0, 0);
    IP4_ADDR(&netif_netmask, 0, 0, 0, 0);
    IP4_ADDR(&netif_gw, 0, 0, 0, 0);

    PRINTF("Initializare LwIP...\r\n");

    lwip_init();

    netif_add(&netif,
              &netif_ipaddr,
              &netif_netmask,
              &netif_gw,
              &enet_config,
              EXAMPLE_NETIF_INIT_FN,
              ethernet_input);

    netif_set_default(&netif);
    netif_set_up(&netif);

    PRINTF("Verific conexiunea Ethernet initiala...\r\n");

    if (ethernetif_wait_linkup(&netif, 5000) == ERR_OK)
    {
        g_ethernet_link_up = true;
        PRINTF("Ethernet conectat.\r\n");

        if (!Network_StartDhcpAndWait(&netif, 20000U))
        {
            PRINTF("Nu am IP DHCP. Colectarea continua in buffer RAM.\r\n");
        }
    }
    else
    {
        g_ethernet_link_up = false;
        PRINTF("Ethernet indisponibil la pornire. Colectarea continua in buffer RAM.\r\n");
    }

    PRINTF("IP placa: %s\r\n", ip4addr_ntoa(netif_ip4_addr(&netif)));
    PRINTF("Gateway:  %s\r\n", ip4addr_ntoa(netif_ip4_gw(&netif)));
    PRINTF("Netmask:  %s\r\n", ip4addr_ntoa(netif_ip4_netmask(&netif)));
    PRINTF("Server:   %d.%d.%d.%d:%d%s\r\n",
           SERVER_IP_1,
           SERVER_IP_2,
           SERVER_IP_3,
           SERVER_IP_4,
           SERVER_PORT,
           SERVER_PATH);

    uint32_t last_sample_ms = sys_now();
    uint32_t last_retry_ms = sys_now();
    uint32_t last_link_check_ms = sys_now();

    PRINTF("\r\nPornesc colectarea.\r\n");
    PRINTF("Interval citire senzor: %u ms.\r\n", SEND_INTERVAL_MS);
    PRINTF("Retry trimitere buffer: %u ms.\r\n", RETRY_INTERVAL_MS);
    PRINTF("Capacitate buffer offline: %u citiri.\r\n", OFFLINE_BUFFER_CAPACITY);
    PRINTF("Filtrare: %u citiri, salt maxim acceptat: %d grade C.\r\n",
           TEMP_SAMPLE_COUNT,
           (int)TEMP_MAX_JUMP_C);

    while (1)
    {
        ethernetif_input(&netif);
        sys_check_timeouts();

        uint32_t now_ms = sys_now();

        if ((uint32_t)(now_ms - last_link_check_ms) >= LINK_CHECK_INTERVAL_MS)
        {
            last_link_check_ms = now_ms;

            bool previous_link_state = g_ethernet_link_up;

            if (ethernetif_wait_linkup(&netif, 10) == ERR_OK)
            {
                g_ethernet_link_up = true;
            }
            else
            {
                g_ethernet_link_up = false;
            }

            if (g_ethernet_link_up && !previous_link_state)
            {
                PRINTF("\r\nEthernet reconectat.\r\n");

                if (!Network_HasIp(&netif))
                {
                    PRINTF("Nu exista IP valid. Reincerc DHCP...\r\n");

                    if (!Network_StartDhcpAndWait(&netif, 20000U))
                    {
                        PRINTF("DHCP a esuat la reconectare. Raman in buffer RAM.\r\n");
                    }
                }

                PRINTF("Se reiau trimiterile catre server.\r\n");
            }
            else if (!g_ethernet_link_up && previous_link_state)
            {
                PRINTF("\r\nEthernet deconectat. Datele vor fi salvate in buffer RAM.\r\n");
            }
        }

        if ((uint32_t)(now_ms - last_sample_ms) >= SEND_INTERVAL_MS)
        {
            last_sample_ms = now_ms;

            uint32_t rawValue = 0U;
            uint32_t millivolts = 0U;
            float measuredTempC = 0.0f;
            float tempC = 0.0f;

            bool readOk = ReadFilteredTemperature(&measuredTempC, &rawValue, &millivolts);

            PRINTF("\r\n--- CITIRE NOUA SENZOR ---\r\n");

            if (!readOk)
            {
                PRINTF("Citire temperatura invalida. Nu se salveaza in buffer.\r\n");
                PRINTF("-------------------------\r\n");
                continue;
            }

            PRINTF("Citire ADC raw medie: %lu\r\n", (unsigned long)rawValue);
            PRINTF("Tensiune AO medie: %lu.%03lu V\r\n",
                   (unsigned long)(millivolts / 1000U),
                   (unsigned long)(millivolts % 1000U));

            PRINTF("Temperatura filtrata initial: ");
            PrintTemperature(measuredTempC);

            bool accepted = ApplyTemperatureAnomalyFilter(measuredTempC, &tempC);

            if (!accepted)
            {
                PRINTF("Citire respinsa ca anomalie. Se salveaza in buffer limitat.\r\n");
                PRINTF("-------------------------\r\n");
                continue;
            }

            PRINTF("Temperatura acceptata: ");
            PrintTemperature(tempC);

            offline_sample_t sample;
            sample.temperatureC = tempC;
            sample.rawValue = rawValue;
            sample.millivolts = millivolts;
            sample.sampleTimeMs = sys_now();
            sample.retryCount = 0U;

            OfflineBuffer_PushBack(sample);

            PRINTF("Citire salvata in buffer RAM.\r\n");
            PRINTF("Timestamp relativ placa: %lu ms\r\n", (unsigned long)sample.sampleTimeMs);
            PRINTF("Citiri in buffer: %lu / %u\r\n",
                   (unsigned long)OfflineBuffer_Count(),
                   OFFLINE_BUFFER_CAPACITY);
            PRINTF("-------------------------\r\n");
        }

        if (g_ethernet_link_up &&
            Network_HasIp(&netif) &&
            !g_http_busy &&
            !OfflineBuffer_IsEmpty() &&
            ((uint32_t)(now_ms - last_retry_ms) >= RETRY_INTERVAL_MS))
        {
            last_retry_ms = now_ms;
            SendNextBufferedPayload();
        }
    }

    return 0;
}
