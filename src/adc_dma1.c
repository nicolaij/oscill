 
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
 
 
#include "driver/adc.h"
#include "esp_adc_cal.h"
//#include "dig_adc.h"
#include "driver/spi_slave_hd.h"
#include "driver/spi_slave.h"
//#include <Arduino.h>
 
//#define USE_ADC_CHANNEL     ADC1_CHANNEL_2 /*!< ADC1 channel 0 is GPIO36 (ESP32), GPIO1 (ESP32-S2) */

#define RCV_HOST    SPI3_HOST
#define DMA_CHAN    RCV_HOST

// some unused pin had to be defined for CS
#define GPIO_CS 0



//  sampling frequency = 40 MHz / (div_num + 1) / sample_interval.
//  use following attenuation values from the enum:
//    ADC_ATTEN_DB_0   = 0,  /*!<No input attenumation, ADC can measure up to approx. 800 mV. */
//    ADC_ATTEN_DB_2_5 = 1,  /*!<The input voltage of ADC will be attenuated, extending the range of measurement to up to approx. 1100 mV. */
//    ADC_ATTEN_DB_6   = 2,  /*!<The input voltage of ADC will be attenuated, extending the range of measurement to up to  approx. 1350 mV. */
//    ADC_ATTEN_DB_11 
//  use following ADC channels from the adc_channel_t enum:
//    ADC_CHANNEL_0    // GPIO1
//    ADC_CHANNEL_1    // GPIO2
//    ADC_CHANNEL_2    // GPIO3
//    ADC_CHANNEL_3    // GPIO4
//    ADC_CHANNEL_4    // GPIO5
//    ADC_CHANNEL_5    // GPIO6
//    ADC_CHANNEL_6    // GPIO7
//    ADC_CHANNEL_7    // GPIO8
//    ADC_CHANNEL_8    // GPIO9
 //   ADC_CHANNEL_9    // GPIO10
esp_err_t adc_sample(uint8_t clkdiv, uint16_t sample_interval, adc_channel_t adc_channel, 
                adc_atten_t att, uint16_t *sample_buf, uint16_t numsamples)
{

    //  controller_clk = (APLL or APB) / (div_num + div_a / div_b + 1).
    adc_digi_clk_t adc_clk { .use_apll = false,     //!<true: use APLL clock; false: use APB clock. 
                             .div_num = clkdiv,          //!<Division factor. Range: 0 ~ 255.
                                                    // Note: When a higher frequency clock is used (the division factor is less than 9),
                                                    // the ADC reading value will be slightly offset. 
                             .div_b = 1,            //!<Division factor. Range: 1 ~ 63. 
                             .div_a = 0};           //!<Division factor. Range: 0 ~ 63.

    adc_digi_pattern_table_t adc1_pattern;
                adc1_pattern.atten=att;   /*!< ADC sampling voltage attenuation configuration. Modification of attenuation affects the range of measurements.
                                         0: measurement range 0 - 800mV,
                                         1: measurement range 0 - 1100mV,
                                         2: measurement range 0 - 1350mV,
                                         3: measurement range 0 - 2600mV. */
                adc1_pattern.channel=adc_channel;   /*!< ADC channel index. */
    
    adc_digi_config_t adc_config = {.conv_limit_en = false,  /*!<Enable the function of limiting ADC conversion times.
                                                            If the number of ADC conversion trigger count is equal to the `limit_num`, the conversion is stopped. */
                                    .conv_limit_num = 0,     /*!<Set the upper limit of the number of ADC conversion triggers. Range: 1 ~ 255. */
                                    .adc1_pattern_len = 1,  /*!<Pattern table length for digital controller. Range: 0 ~ 16 (0: Don't change the pattern table setting).
                                                            The pattern table that defines the conversion rules for each SAR ADC. Each table has 16 items, in which channel selection,
                                                            resolution and attenuation are stored. When the conversion is started, the controller reads conversion rules from the
                                                            pattern table one by one. For each controller the scan sequence has at most 16 different rules before repeating itself. */
                                    .adc2_pattern_len = 0,  /*!<Refer to ``adc1_pattern_len`` */
                                    .adc1_pattern = &adc1_pattern,      /*!<Pointer to pattern table for digital controller. The table size defined by `adc1_pattern_len`. */
                                    .adc2_pattern = 0,      /*!<Refer to `adc1_pattern` */
                                    .conv_mode = ADC_CONV_SINGLE_UNIT_1, 
                                    .format = ADC_DIGI_FORMAT_12BIT,  
                                    .interval = sample_interval,  //!<The number of interval clock cycles for the digital controller to trigger the measurement.
                                                            // The unit is the divided clock. Range: 40 ~ 4095.
                                                            // Expression: `trigger_meas_freq` = `controller_clk` / 2 / interval. Refer to ``adc_digi_clk_t``.
                                                            // Note: The sampling rate of each channel is also related to the conversion mode (See ``adc_digi_convert_mode_t``) and pattern table settings. */
                                    .dig_clk = adc_clk,     //!<ADC digital controller clock divider settings. Refer to ``adc_digi_clk_t``.
                                                            // Note: The clocks of the DAC digital controller use the ADC digital controller clock divider. 
                                    .dma_eof_num = numsamples}; //!<DMA eof num of adc digital controller.
                                                                //If the number of measurements reaches `dma_eof_num`, then `dma_in_suc_eof` signal is generated in DMA.
                                                                //Note: The converted d}
 

    spi_bus_config_t spi_bus_cfg = { .mosi_io_num = -1,                ///< GPIO pin for Master Out Slave In (=spi_d) signal, or -1 if not used.
                                     .miso_io_num = -1,                ///< GPIO pin for Master In Slave Out (=spi_q) signal, or -1 if not used.
                                     .sclk_io_num = -1,                ///< GPIO pin for Spi CLocK signal, or -1 if not used.
                                     .quadwp_io_num = -1,              ///< GPIO pin for WP (Write Protect) signal which is used as D2 in 4-bit communication modes, or -1 if not used.
                                     .quadhd_io_num = -1,
                                     .max_transfer_sz = numsamples*2};

    spi_slave_hd_callback_config_t cb_config;
    spi_slave_hd_slot_config_t spi_slot_cfg = { .spics_io_num=GPIO_CS,
                                                .flags=0,
                                                .mode=0,                                                
                                                .queue_size=1,
                                                .dma_chan =  DMA_CHAN,
                                                .cb_config=cb_config};


    esp_err_t err = spi_slave_hd_init(RCV_HOST, &spi_bus_cfg, &spi_slot_cfg);
    if(err) return err;

    err = adc_digi_init();
    if(err) return err;

    err = adc_digi_controller_config(&adc_config);
    if(err) return err;

    //err = adc_digi_start();
    //if(err) return err;

    printf("Init Done"); 

    spi_slave_hd_data_t rx_trans {
        .data = (uint8_t*)sample_buf,       ///< Buffer to send, must be DMA capable
        .len = (size_t)(numsamples<<1),     ///< Len of data to send/receive. For receiving the buffer length should be multiples of 4 bytes, otherwise the extra part will be truncated.
        .trans_len = numsamples,            ///< Data actually received
        .arg = 0};                          ///< Extra argument indiciating this data


    err = spi_slave_hd_queue_trans(RCV_HOST, SPI_SLAVE_CHAN_RX, &rx_trans, 1000);
    if(err) return err;

    err = adc_digi_start();
    if(err) return err;

    else {
        // wait for data to be complete
        delay(((clkdiv+1)*sample_interval * numsamples) / 40000 + 1);

        printf("Received Data:");
        for(int i=0; i < numsamples; ++i) {
            printf(sample_buf[i]);
        }
    }

    spi_slave_hd_deinit(RCV_HOST);
    return err;

}
