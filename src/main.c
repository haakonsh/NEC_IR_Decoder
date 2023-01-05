/*
 * Copyright (c) 2022, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log.h>
#include <nrfx_spim.h>
#include <nrfx_rtc.h>
#include <nrfx_gpiote.h>
#include <nrfx_ppi.h>
#include <debug/cpu_load.h>

LOG_MODULE_REGISTER(nec_ir);

#define MOSI_PIN    NRF_GPIO_PIN_MAP(0, 26)
#define MISO_PIN    NRF_GPIO_PIN_MAP(0, 29)
#define SCK_PIN     NRF_GPIO_PIN_MAP(0, 27)
#define CS_PIN      NRFX_SPIM_PIN_NOT_USED
#define SPIM_BUF_SIZE 1024
#define SPIM_INST_IDX 0
nrfx_spim_t spim_inst = NRFX_SPIM_INSTANCE(SPIM_INST_IDX);

#define RTC_INST_IDX 2
nrfx_rtc_t rtc_inst = NRFX_RTC_INSTANCE(RTC_INST_IDX);

uint8_t gpiote_channel_up;
uint8_t gpiote_channel_down;
nrf_ppi_channel_t ppi_rtc_start_ch;
nrf_ppi_channel_t ppi_rtc_stop_ch;
nrf_ppi_channel_t ppi_rtc_compare_ch;
nrf_ppi_channel_t ppi_spim_start_ch; 
nrf_ppi_channel_group_t ppi_spim_start_ch_group;

nrfx_spim_config_t spim_config = 
{                                                                           \
    .sck_pin        = SCK_PIN,                                              \
    .mosi_pin       = MOSI_PIN,                                             \
    .miso_pin       = MISO_PIN,                                             \
    .ss_pin         = CS_PIN,                                               \
    .ss_active_high = false,                                                \
    .irq_priority   = NRFX_SPIM_DEFAULT_CONFIG_IRQ_PRIORITY,                \
    .orc            = 0xFF,                                                 \
    .frequency      = NRF_SPIM_FREQ_125K,                                   \
    .mode           = NRF_SPIM_MODE_0,                                      \
    .bit_order      = NRF_SPIM_BIT_ORDER_MSB_FIRST,                         \
    .miso_pull      = NRF_GPIO_PIN_NOPULL,                                  \
    .dcx_pin        = NRFX_SPIM_PIN_NOT_USED,                               \
    .rx_delay       = 0x00,                                                 \
    .use_hw_ss      = false,                                                \
    .ss_duration    = 0x00                                                  \
};

nrfx_rtc_config_t rtc_config = 
{                                                                \
    .prescaler          = RTC_FREQ_TO_PRESCALER(32768),          \
    .interrupt_priority = NRFX_RTC_DEFAULT_CONFIG_IRQ_PRIORITY,  \
    .tick_latency       = NRFX_RTC_US_TO_TICKS(2000, 32768),     \
    .reliable           = false,                                 \
};

static const nrfx_gpiote_input_config_t gpiote_input_cfg = {.pull = NRF_GPIO_PIN_NOPULL};

nrfx_gpiote_trigger_config_t gpiote_input_trigger_up_cfg = 
{
    .trigger =NRFX_GPIOTE_TRIGGER_LOTOHI, ///< Low to high edge trigger.
    .p_in_channel = &gpiote_channel_up
};

nrfx_gpiote_trigger_config_t gpiote_input_trigger_down_cfg = 
{
    .trigger =NRFX_GPIOTE_TRIGGER_HITOLO, ///< Low to high edge trigger.
    .p_in_channel = &gpiote_channel_down
};

static uint8_t m_tx_buffer[2] = {0x00};
static uint8_t m_rx_buffer[SPIM_BUF_SIZE];
static uint8_t nec_ir_buffer[SPIM_BUF_SIZE];
bool nec_ir_transfer_done = false;

/*struct nec_ir_data_t {
    void *fifo_reserved;
    uint8_t data[SPIM_BUF_SIZE];
    uint16_t len;
};*/

nrfx_spim_xfer_desc_t spim_xfer_desc = NRFX_SPIM_XFER_TRX(m_tx_buffer, sizeof(m_tx_buffer),
                                                          m_rx_buffer, sizeof(m_rx_buffer));

void rtc_handler(nrfx_rtc_int_type_t int_type){}

static void spim_handler(nrfx_spim_evt_t const * p_event, void * p_context)
{   
    if (p_event->type == NRFX_SPIM_EVENT_DONE)
    {        
        nec_ir_transfer_done = true;       
    }
}

void nec_ir_decode(uint8_t * buf,uint16_t buf_size)
{
    uint8_t     addr        = 0;
    uint8_t     data        = 0;
    uint8_t     zero_cnt    = 0;
    uint8_t     bit_cnt     = 0;
    uint32_t    packet      = 0;
    //static uint8_t packet_loss_check = 0;

    /* 
     * This loop counts the number of samples of value 0 that we've received in sucession,
     * and evaluates whether we believe it is a logical 1 or 0. It does this for each 'bit'
     * in the signal and builds a 32-bit vaiable representing the received packet
     */
    for(uint16_t i = 0; i < buf_size; i++)
    {
        if(buf[i] == 0)
        {
            zero_cnt++;
        }
        else
        {
            if(zero_cnt > 16)
            {
                //logic 1
                packet |= (1 << bit_cnt++);
            }
            else if(zero_cnt > 4)
            {
                //logic 0
                bit_cnt++;
            }
            zero_cnt = 0;
        }                 
    }

    /*
     * This block evaluates whether the address and data fields matches their respective logical inverses
     */
    if((((packet & 0x0000FF00) >> 8) + ((packet & 0x000000FF) >> 0)) == 0xFF)
    {
        addr = ((packet & 0x000000FF) >> 0);
    }
    else{LOG_INF("Address is not equal to its logical inverse! addr: %u  !addr: %u", addr, (packet & 0x0000FF00));}

    if((((packet & 0xFF000000) >> 24) + ((packet & 0x00FF0000) >> 16)) == 0xFF)
    {
        data = ((packet & 0x00FF0000) >> 16);
    }
    else{LOG_INF("Data is not equal to its logical inverse! data: %u  !data: %u", data, (packet & 0xFF000000));}

    /*
     * The test signal have a const address of 0xF0 and a data byte that increments for each packet sent. Remove in prod.
     * Ref: https://github.com/haakonsh/NEC_IR_Encoder.git.
     */
    /*if((++packet_loss_check != data) || (addr != 0xF0))
    {
        LOG_INF("Packet lost!!!");
        packet_loss_check = data;
    }*/

    printk("NEC IR packet received; Address: %u Data: %u\n", addr, data);
}

int main(void)
{
    //Initialize the CPU load logger
    LOG_INIT();    
    if(cpu_load_init() != 0)printk("cpu_load_init() failed!\n");

    nrfx_err_t status;
    (void)status;    

    status = nrfx_spim_init(&spim_inst, &spim_config, spim_handler, NULL);
    NRFX_ASSERT(status == NRFX_SUCCESS);

#if defined(__ZEPHYR__)
    #define SPIM_INST         NRFX_CONCAT_2(NRF_SPIM, SPIM_INST_IDX)
    #define SPIM_INST_HANDLER NRFX_CONCAT_3(nrfx_spim_, SPIM_INST_IDX, _irq_handler)
    IRQ_DIRECT_CONNECT(NRFX_IRQ_NUMBER_GET(SPIM_INST), IRQ_PRIO_LOWEST, SPIM_INST_HANDLER, 0);
#endif

    memset(m_rx_buffer, 0 , sizeof(m_rx_buffer));
    memset(nec_ir_buffer, 0 , sizeof(nec_ir_buffer));
    
    status = nrfx_spim_xfer(&spim_inst, &spim_xfer_desc, NRFX_SPIM_FLAG_HOLD_XFER);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_rtc_init(&rtc_inst, &rtc_config, rtc_handler);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_rtc_cc_set(&rtc_inst, NRFX_RTC_INT_COMPARE0, 250, false); //Compare after 250/32768 = ~7.5ms
    NRFX_ASSERT(status == NRFX_SUCCESS);

#if defined(__ZEPHYR__)
    #define RTC_INST         NRFX_CONCAT_2(NRF_RTC, RTC_INST_IDX)
    #define RTC_INST_HANDLER NRFX_CONCAT_3(nrfx_rtc_, RTC_INST_IDX, _irq_handler)
    IRQ_DIRECT_CONNECT(NRFX_IRQ_NUMBER_GET(RTC_INST), IRQ_PRIO_LOWEST, RTC_INST_HANDLER, 0);
#endif

    status = nrfx_gpiote_channel_alloc(&gpiote_channel_up);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_gpiote_channel_alloc(&gpiote_channel_down);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_gpiote_input_configure(MISO_PIN, &gpiote_input_cfg, &gpiote_input_trigger_up_cfg, NULL);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_gpiote_input_configure(MISO_PIN, &gpiote_input_cfg, &gpiote_input_trigger_down_cfg, NULL);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_ppi_channel_alloc(&ppi_rtc_start_ch);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_ppi_channel_alloc(&ppi_rtc_stop_ch);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_ppi_channel_alloc(&ppi_rtc_compare_ch);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_ppi_channel_alloc(&ppi_spim_start_ch);
    NRFX_ASSERT(status == NRFX_SUCCESS);
    
    status = nrfx_ppi_group_alloc(&ppi_spim_start_ch_group);

    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_ppi_channel_include_in_group(ppi_spim_start_ch, ppi_spim_start_ch_group);

    NRFX_ASSERT(status == NRFX_SUCCESS);

    //Start RTC2 on rising edge of MISO_PIN
    status = nrfx_ppi_channel_assign(ppi_rtc_start_ch, 
                                    nrf_gpiote_event_address_get(NRF_GPIOTE, 
                                    nrf_gpiote_in_event_get(gpiote_channel_up)),
                                    nrfx_rtc_task_address_get(&rtc_inst, NRF_RTC_TASK_START));
    NRFX_ASSERT(status == NRFX_SUCCESS);

    //Stop RTC2 on falling edge of MISO_PIN
    status = nrfx_ppi_channel_assign(ppi_rtc_stop_ch, 
                                    nrf_gpiote_event_address_get(NRF_GPIOTE, 
                                    nrf_gpiote_in_event_get(gpiote_channel_down)),
                                    nrfx_rtc_task_address_get(&rtc_inst, NRF_RTC_TASK_STOP));
    NRFX_ASSERT(status == NRFX_SUCCESS);

    //Clear RTC2 on falling edge of MISO_PIN
    status = nrfx_ppi_channel_fork_assign(ppi_rtc_stop_ch, 
                                        nrfx_rtc_task_address_get(&rtc_inst, NRF_RTC_TASK_CLEAR));
    NRFX_ASSERT(status == NRFX_SUCCESS);

    //Enable ppi_spim_start_ch_group on RTC2's COMPARE0 event.This event indicates that an NEC IR start frame was detected.   
    status = nrfx_ppi_channel_assign(ppi_rtc_compare_ch, 
                                    nrfx_rtc_event_address_get(&rtc_inst, NRF_RTC_EVENT_COMPARE_0),
                                    nrfx_ppi_task_addr_group_enable_get(ppi_spim_start_ch_group));
                                
    NRFX_ASSERT(status == NRFX_SUCCESS);

    //Start SPIM0 transfer on a rising flank on MISO.
    status = nrfx_ppi_channel_assign(ppi_spim_start_ch, 
                                    nrf_gpiote_event_address_get(NRF_GPIOTE, 
                                    nrf_gpiote_in_event_get(gpiote_channel_up)),
                                    nrfx_spim_start_task_get(&spim_inst));
    NRFX_ASSERT(status == NRFX_SUCCESS);

    /*
     * Disable ppi_spim_start_ch_group on a rising flank on MISO. We only need ppi_spim_start_ch to trigger     \
     * an event on the first rising edge after RTC2's COMPARE0 event.                                           \
     */
    status = nrfx_ppi_channel_fork_assign(ppi_spim_start_ch, 
                                        nrfx_ppi_task_addr_group_disable_get(ppi_spim_start_ch_group));                                    
    NRFX_ASSERT(status == NRFX_SUCCESS);
 
    status = nrfx_ppi_channel_enable(ppi_rtc_start_ch);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_ppi_channel_enable(ppi_rtc_stop_ch);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    status = nrfx_ppi_channel_enable(ppi_rtc_compare_ch);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    //Enable the state-machine
    nrf_gpiote_event_enable(NRF_GPIOTE, gpiote_channel_up);
    nrf_gpiote_event_enable(NRF_GPIOTE, gpiote_channel_down);

    while (1)
    {
        if(nec_ir_transfer_done)
        {   
            nec_ir_transfer_done = false;

            memcpy(nec_ir_buffer, m_rx_buffer , sizeof(nec_ir_buffer));
            memset(m_rx_buffer, 0 , sizeof(m_rx_buffer));

            status = nrfx_spim_xfer(&spim_inst, &spim_xfer_desc, NRFX_SPIM_FLAG_HOLD_XFER);
            NRFX_ASSERT(status == NRFX_SUCCESS);

            nec_ir_decode(nec_ir_buffer, sizeof(nec_ir_buffer));
            
            memset(nec_ir_buffer, 0 , sizeof(nec_ir_buffer));                                
        }
        // Go to sleep unless there are pending logs that need to be processed by the log module.
        if(LOG_PROCESS() == false)
        {
            k_cpu_idle();
        }
    }
}

/** @} */
