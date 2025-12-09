#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <stdio.h>
#include <string.h>

#include "main.h"

#include "u8g2_esp32_hal.h"
#include <u8g2.h>

#include "ui.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_sleep.h"

#define PIN_SDA 8
#define PIN_SCL 9

static const char *TAG = "ui";

extern int menu[];

#define MENU_LINES sizeof(menu) / sizeof(menu[0])

int menu_current_selection = -1;
int menu_current_position = 0;
int menu_current_display = 0;

u8g2_t u8g2;

#define TIMEOUT (60 * 1000 / 40)

int timeout_counter = 0;

int limits(int val, int min, int max)
{
	if (val < min)
		return min;
	if (val > max)
		return max;
	return val;
}

void task_SSD1306i2c(void *ignore)
{
	u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	u8g2_esp32_hal.sda = PIN_SDA;
	u8g2_esp32_hal.scl = PIN_SCL;
	u8g2_esp32_hal_init(u8g2_esp32_hal);

	u8g2_t u8g2; // a structure which will contain all the data for one display
	u8g2_Setup_ssd1306_i2c_128x32_univision_f(
		&u8g2, U8G2_R0,
		// u8x8_byte_sw_i2c,
		u8g2_esp32_i2c_byte_cb,
		u8g2_esp32_gpio_and_delay_cb); // init u8g2 structure
	u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);

	ESP_LOGI(TAG, "u8g2_InitDisplay");
	u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in
							 // sleep mode after this,

	u8g2_SetPowerSave(&u8g2, 0); // wake up display

	u8g2_ClearBuffer(&u8g2);

	u8g2_DrawFrame(&u8g2, 32, 0, 64, 32);

	u8g2_SendBuffer(&u8g2);

	vTaskDelay(1000 / portTICK_PERIOD_MS);

	/*
	0x26, X0=0, горизонтальный сдвиг вправо,
	0x27, X0=1, горизонтальный сдвиг влево (на один столбец).
	A пустой байт.
	B[2:0] определяет адрес начальной страницы:
	000 PAGE0, .., 111 PAGE7.
	C[2:0] установка интервала времени между каждым шагом в количестве кадров:
	000 5 кадров, 001 - 64 кадра, 010 - 128 кадров, 011 - 256 кадров, 100 - 3 кадра, 101 - 4 кадра, 110 - 25 кадров, 111 - 2 кадра.
	D[2:0] определяет адрес конечной страницы так же, как и B[2:0]. Однако значение D[2:0] должно быть больше или равно B[2:0].
	E и F пустые байты.
	*/
	// u8g2_SendF(&u8g2, "caaaaaac", 0x26, 0, 0, 0b111, 3, 0, 255, 0x2f);
	u8g2_SendF(&u8g2, "caaaaaa", 0x26, 0, 0, 0b111, 3, 0, 255);

	//u8g2_ClearBuffer(&u8g2);
	//u8g2_SendBuffer(&u8g2);

	// Vertical addressing mode: (A[1:0]=01b)
	u8g2_SendF(&u8g2, "ca", 0x20, 0b01);
	u8g2_SendF(&u8g2, "caa", 0x21, 0, 0); // COL 0
	u8g2_SendF(&u8g2, "caa", 0x22, 0, 3); // PAGE 0-3

	uint8_t val = 0;
	while (1)
	{
		if (pdTRUE != xQueueReceive(ui_queue, &val, (TickType_t)0))
		{
			val = 0;
		}

		if (val > 31)
			val = 31;

		uint32_t pixel = 1 << (31 - val);

		u8g2_SendF(&u8g2, "cddddc", 0x2e, pixel, pixel >> 8, pixel >> 16, pixel >> 24, 0x2f);

		vTaskDelay(1);
	};
//test
	unsigned int i = 1;
	unsigned int n = 0;
	while (1)
	{
		if (n < 32)
			i = 1 << n;
		else
			i = 1 << (62 - n);

		u8g2_SendF(&u8g2, "cddddc", 0x2e, i & 0xFF, i >> 8, i >> 16, i >> 24, 0x2f);

		n++;
		if (n >= 62)
			n = 0;

		vTaskDelay(1);
	}

	/*
		ESP_LOGI(TAG, "u8g2_ClearBuffer");
		u8g2_ClearBuffer(&u8g2);
		ESP_LOGI(TAG, "u8g2_DrawBox");
		u8g2_DrawBox(&u8g2, 0, 26, 80, 6);
		u8g2_DrawFrame(&u8g2, 0, 26, 100, 6);

		ESP_LOGI(TAG, "u8g2_SetFont");
		u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);
		ESP_LOGI(TAG, "u8g2_DrawStr");
		u8g2_DrawStr(&u8g2, 2, 17, "Hi nkolban!");
		ESP_LOGI(TAG, "u8g2_SendBuffer");
		u8g2_SendBuffer(&u8g2);

		ESP_LOGI(TAG, "All done!");
	*/

	while (1)
	{
		vTaskDelay(10000 / portTICK_PERIOD_MS);
	}

	vTaskDelete(NULL);
}

void ui_task(void *arg)
{
	int screen = 0;
	int encoder_cor = 0;

	char buf[64];

	// Rotary encoder underlying device is represented by a PCNT unit in this example
	uint32_t pcnt_unit = 0;

	// initialize the u8g2 hal
	u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	u8g2_esp32_hal.sda = PIN_SDA;
	u8g2_esp32_hal.scl = PIN_SCL;
	u8g2_esp32_hal_init(u8g2_esp32_hal);

	// initialize the u8g2 library
	u8g2_Setup_sh1106_i2c_128x64_noname_f(
		// u8g2_Setup_ssd1306_i2c_128x32_univision_f(
		&u8g2,
		U8G2_R0,
		u8g2_esp32_i2c_byte_cb,
		u8g2_esp32_gpio_and_delay_cb);

	// set the display address
	u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);

	// initialize the display
	ESP_LOGI(TAG, "u8g2_InitDisplay");
	u8g2_InitDisplay(&u8g2);

	// wake up the display
	u8g2_SetPowerSave(&u8g2, 0);

	// draw the hourglass animation, full-half-empty
	u8g2_ClearBuffer(&u8g2);
	u8g2_DrawXBM(&u8g2, 34, 2, 60, 60, hourglass_full);
	u8g2_SendBuffer(&u8g2);
	vTaskDelay(500 / portTICK_PERIOD_MS);

	u8g2_ClearBuffer(&u8g2);
	u8g2_DrawXBM(&u8g2, 34, 2, 60, 60, hourglass_half);
	u8g2_SendBuffer(&u8g2);
	vTaskDelay(500 / portTICK_PERIOD_MS);

	u8g2_ClearBuffer(&u8g2);
	u8g2_DrawXBM(&u8g2, 34, 2, 60, 60, hourglass_empty);
	u8g2_SendBuffer(&u8g2);

	vTaskDelay(500 / portTICK_PERIOD_MS);
	/*

		int encoder_val = 0;
		bool update = true;


		int64_t t1 = 0; // Для определения Double Click

		BaseType_t xResult;
		uint32_t ulNotifiedValue;
		// loop
		while (1)
		{

			if (xResult == pdPASS)
			{

				if ((ulNotifiedValue & 1) != 0)
				{
					u8g2_ClearBuffer(&u8g2);
					u8g2_SendBuffer(&u8g2);
				}

				if ((ulNotifiedValue & 2) != 0)
				{
					ESP_LOGI(TAG, "u8g2_SetPowerSave");
					u8g2_SetPowerSave(&u8g2, true);
					vTaskDelay(10000 / portTICK_PERIOD_MS);
				}
			}

			int s = gpio_get_level(PIN_ENCODER_BTN);
			if (s == 0) // down
			{
				enc_btn++;
				if (enc_btn == 50) // long press
				{
					if (t1 == 0)
						encoder_key = KEY_LONG_PRESS;
				}

				if (enc_btn == 100) // super long press
				{
					if (t1 == 0)
						encoder_key = KEY_DOUBLELONG_PRESS;
				}
			}
			else // up
			{
				if (enc_btn > 0)
				{
					if (enc_btn < 10) // short click
					{
						if (t1 > 0)
						{
							// printf("Double t :%lld, c=%d\n", esp_timer_get_time() - t1, enc_btn);
							encoder_key = KEY_DOUBLECLICK;
							t1 = 0;
						}
						else
							t1 = esp_timer_get_time();
					}
					else
						t1 = 0;
				}
				else if (t1 > 0)
					if (esp_timer_get_time() - t1 > 400000)
					{
						encoder_key = KEY_CLICK;
						t1 = 0;
					}

				enc_btn = 0;
			}

			if (encoder->get_counter_value(encoder) != encoder_val)
			{
				encoder_val = encoder->get_counter_value(encoder);

				int v = encoder_val / 4 + encoder_cor;

				if (screen == 0)
				{
					cmd.power = limits(v, PWM_MIN, PWM_MAX);
					encoder_cor = cmd.power - encoder_val / 4;
					xQueueSend(uicmd_queue, &cmd, (TickType_t)0);
				}
				else if (screen == 1)
				{
					if (menu_current_selection < 0) //навигация по меню
					{
						menu_current_position = limits(v, 0, MENU_LINES - 1);
						encoder_cor = menu_current_position - encoder_val / 4;
					}
					else //изменения значений
					{
						menu[menu_current_selection].val = limits(v, menu[menu_current_selection].min, menu[menu_current_selection].max);
					}
				};
				update = true;
				timeout_counter = 0;
			}

			if (screen == 0)
			{
				switch (encoder_key)
				{
				case KEY_CLICK:
					if (cmd.cmd == 1) // ON
					{
						cmd.cmd = 0; // OFF
					}
					else
					{
						if (cmd.cmd == 0) // OFF
						{
							cmd.cmd = 2; // PULSE
						}
					}
					update = true;
					timeout_counter = 0;
					xQueueSend(uicmd_queue, &cmd, (TickType_t)0);
					break;
				case KEY_DOUBLECLICK: //Включаем
					if (cmd.cmd == 1) // ON
					{
						cmd.cmd = 0; // OFF
					}
					else
						cmd.cmd = 1;
					update = true;
					timeout_counter = 0;
					xQueueSend(uicmd_queue, &cmd, (TickType_t)0);
					break;
				case KEY_LONG_PRESS:
					//Вызываем меню
					screen++;
					if (screen == 2)
						screen = 0;

					if (screen == 1)
					{
						menu_current_position = 0;
						encoder_cor = menu_current_position - encoder_val / 4;
						menu_current_selection = -1;
					}
					update = true;
					timeout_counter = 0;
					break;
				default:
					break;
				}
			}
			else if (screen == 1) //Меню
			{
				switch (encoder_key)
				{
				case KEY_CLICK:
					if (menu_current_selection < 0) //Выбираем пункт меню
					{
						if (menu_current_position == MENU_LINES - 1) //Последний пункт меню - Выход на главный экран
						{
							screen = 0;
							encoder_cor = cmd.power - encoder_val / 4;
						}
						else
						{
							menu_current_selection = menu_current_position;
							encoder_cor = menu[menu_current_selection].val - encoder_val / 4;
						}
					}
					else //Сохраняем введенное значение
					{
						nvs_handle_t my_handle;
						esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
						if (err != ESP_OK)
						{
							printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
						}
						else
						{
							// Write
							printf("Write: \"%s\" ", menu[menu_current_selection].name);
							err = nvs_set_i32(my_handle, menu[menu_current_selection].name, menu[menu_current_selection].val);
							printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

							// Commit written value.
							// After setting any values, nvs_commit() must be called to ensure changes are written
							// to flash storage. Implementations may write to storage at other times,
							// but this is not guaranteed.
							printf("Committing updates in NVS ... ");
							err = nvs_commit(my_handle);
							printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

							// Close
							nvs_close(my_handle);
						}

						menu_current_selection = -1;
						encoder_cor = menu_current_position - encoder_val / 4;
					}
					update = true;
					timeout_counter = 0;
					break;
				case KEY_LONG_PRESS: //Выход на главный экран
					screen = 0;
					encoder_cor = cmd.power - encoder_val / 4;
					update = true;
					timeout_counter = 0;
					break;
				default:
					break;
				}
			}

			encoder_key = 0;

			if (pdTRUE == xQueueReceive(adc1_queue, &result, (TickType_t)0))
			{
				update = true;
				if (cmd.cmd == 2)
					cmd.cmd = 0;
			}

			//Обновляем экран
			if (update)
			{
				xSemaphoreTake(i2c_mux, portMAX_DELAY);

				if (screen == 0)
				{
					u8g2_ClearBuffer(&u8g2);
					u8g2_SetFont(&u8g2, u8g2_font_unifont_t_cyrillic);

					u8g2_DrawStr(&u8g2, 2, 16 - 3, "POWER: ");

					if (cmd.cmd == 0)
						u8g2_DrawStr(&u8g2, 53, 16 - 3, "OFF");
					if (cmd.cmd == 1)
						u8g2_DrawStr(&u8g2, 53, 16 - 3, "ON");
					if (cmd.cmd == 2)
						u8g2_DrawStr(&u8g2, 53, 16 - 3, "PULSE");

					// u8g2_DrawStr(&u8g2, 2, 31, "PWM: ");
					// u8g2_DrawStr(&u8g2, 60, 31, u8x8_u16toa(pwm, 3));
					sprintf(buf, "%d", cmd.power);
					int w = u8g2_GetStrWidth(&u8g2, buf);
					u8g2_DrawStr(&u8g2, u8g2_GetDisplayWidth(&u8g2) - w, 16 - 3, buf);

					sprintf(buf, "U = %d V", result.U);
					u8g2_DrawStr(&u8g2, 2, 16 * 2 - 3, buf);
					sprintf(buf, "R = %d kOm", result.R);
					u8g2_DrawStr(&u8g2, 2, 16 * 3 - 3, buf);

					u8g2_SetFont(&u8g2, u8g2_font_6x13_t_cyrillic);
					sprintf(buf, "1:%4d 2:%4d U:%4d", result.adc11, result.adc12, result.adc2);
					u8g2_DrawStr(&u8g2, 2, 16 * 4 - 3, buf);

					u8g2_SendBuffer(&u8g2);
				}
				if (screen == 1) //Меню настроек
				{
					u8g2_ClearBuffer(&u8g2);
					u8g2_SetFontMode(&u8g2, 1);
					u8g2_SetFont(&u8g2, u8g2_font_unifont_t_cyrillic);
					// u8g2_SetDrawColor(&u8g2, 1);
					u8g2_SetDrawColor(&u8g2, 2);

					if (menu_current_position - menu_current_display > (u8g2.height / 16 - 1))
						menu_current_display = menu_current_position - (u8g2.height / 16 - 1);

					if (menu_current_position < menu_current_display)
						menu_current_display = menu_current_position;

					if (menu_current_selection < 0) //Навигация по меню
					{
						u8g2_DrawBox(&u8g2, 0, 16 * (menu_current_position - menu_current_display), u8g2_GetDisplayWidth(&u8g2), 16);
					}

					for (int l = 0; l < u8g2.height / 16; l++)
					{
						int p = menu_current_display + l;
						if (p < MENU_LINES)
						{
							u8g2_DrawUTF8(&u8g2, 3, 16 * (l + 1) - 3, menu[p].name);
							sprintf(buf, "%d", menu[p].val);
							int w = u8g2_GetStrWidth(&u8g2, buf);
							if (menu_current_selection == p)
							{
								u8g2_DrawBox(&u8g2, u8g2_GetDisplayWidth(&u8g2) - w - 2, 16 * l, w + 2, 16);
							}
							u8g2_DrawUTF8(&u8g2, u8g2_GetDisplayWidth(&u8g2) - w, 16 * (l + 1) - 3, buf);
						}
					}
					u8g2_SendBuffer(&u8g2);
				}
				xSemaphoreGive(i2c_mux);
			}
			update = false;
			vTaskDelay(40 / portTICK_PERIOD_MS);

			if (++timeout_counter > TIMEOUT)
			{
				u8g2_SetPowerSave(&u8g2, true);
				//засыпаем...
				go_sleep();
			}
		}

		*/
}

void reset_sleep_timeout()
{
	timeout_counter = 0;
}