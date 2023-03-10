/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include "nokia5110_LCD.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MAX_SIZE_BUFFER 1024
#define MAX_SIZE_CMD 132
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim10;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
//RX BUFFER
uint8_t USART_RX_BUFFER[MAX_SIZE_BUFFER];
__IO int USART_RX_Empty = 0;
__IO int USART_RX_Busy = 0;

//TX BUFFER
uint8_t USART_TX_BUFFER[MAX_SIZE_BUFFER];
__IO int USART_TX_Empty = 0;
__IO int USART_TX_Busy = 0;

//FRAME
char FRAME[MAX_SIZE_CMD];
int frame_idx;
int frame_status = 0;

//RTC INIT
RTC_TimeTypeDef sTime = { 0 };
RTC_DateTypeDef sDate = { 0 };
RTC_AlarmTypeDef sAlarm = { 0 };

//ALARM
RTC_AlarmTypeDef Alarm[7][3];

int AlarmActive[7][3];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_RTC_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM10_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int check_sum_command(char *msg) {
	int mod = 0;
	for (int i = 0; i < strlen(msg); i++) {
		mod = mod + msg[i];
	}
	return mod % 256;
}

void USART_fsend_encode(char message[], char output[]) {
	int checkSumMessage = check_sum_command(message);
	sprintf(output, "#%s\r\n%02X:", message, checkSumMessage);
}

void USART_fsend(char *format, ...) {
	char tmp_rs[132];
	int i;
	__IO int idx;
	va_list arglist;
	va_start(arglist, format);
	vsprintf(tmp_rs, format, arglist);
	va_end(arglist);
	char frame_message[132] = { 0 };
	USART_fsend_encode(tmp_rs, frame_message);
	idx = USART_TX_Empty;
	for (i = 0; i < strlen(frame_message); i++) {
		USART_TX_BUFFER[idx] = frame_message[i];
		idx++;
		if (idx >= MAX_SIZE_BUFFER) {
			idx = 0;
		}
	}
	__disable_irq();

	if ((USART_TX_Empty == USART_TX_Busy)
			&& (__HAL_UART_GET_FLAG(&huart2,UART_FLAG_TXE) == SET)) {
		USART_TX_Empty = idx;
		uint8_t tmp = USART_TX_BUFFER[USART_TX_Busy];
		USART_TX_Busy++;
		if (USART_TX_Busy >= MAX_SIZE_BUFFER) {
			USART_TX_Busy = 0;
		}
		HAL_UART_Transmit_IT(&huart2, &tmp, 1);
	} else {
		USART_TX_Empty = idx;
	}
	__enable_irq();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart == &huart2) {
		USART_RX_Empty++;
		if (USART_RX_Empty >= MAX_SIZE_BUFFER) {
			USART_RX_Empty = 0;
		}
		HAL_UART_Receive_IT(&huart2, &USART_RX_BUFFER[USART_RX_Empty], 1);
	}
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart == &huart2) {
		if (USART_TX_Empty != USART_TX_Busy) {
			uint8_t tmp = USART_TX_BUFFER[USART_TX_Busy];
			USART_TX_Busy++;
			if (USART_TX_Busy >= MAX_SIZE_BUFFER) {
				USART_TX_Busy = 0;
			}
			HAL_UART_Transmit_IT(&huart2, &tmp, 1);
		}
	}
}

uint8_t USART_kbhit() {
	if (USART_RX_Empty == USART_RX_Busy) {
		return 0;
	} else {
		return 1;
	}
}

uint8_t USART_getchar() {
	uint8_t tmp;
	if (USART_RX_Empty != USART_RX_Busy) {
		tmp = USART_RX_BUFFER[USART_RX_Busy];
		USART_RX_Busy++;
		if (USART_RX_Busy >= MAX_SIZE_BUFFER) {
			USART_RX_Busy = 0;
		}
		return tmp;
	} else {
		return 0;
	}
}

int convertHexToDecimal(char num[]) {
	int len = strlen(num);
	int base = 1;
	int temp = 0;
	for (int i = len - 1; i >= 0; i--) {
		if (num[i] >= '0' && num[i] <= '9') {
			temp += (num[i] - 48) * base;
			base = base * 16;
		} else if (num[i] >= 'A' && num[i] <= 'F') {
			temp += (num[i] - 55) * base;
			base = base * 16;
		}
	}
	return temp;
}

void setAlarm();

void showTime() {
	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
	USART_fsend("%02d.%02d.%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
}

void setTime(int hours, int minutes, int seconds) {
	sTime.Seconds = seconds;
	sTime.Minutes = minutes;
	sTime.Hours = hours;
	HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	setAlarm();
}

void showDate() {
	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
	USART_fsend("%02d/%02d/20%02d", sDate.Date, sDate.Month, sDate.Year);
	USART_fsend("%d", sDate.WeekDay);
}

void setDate(int year, int month, int day, int weekDay) {

	sDate.Date = day;
	sDate.Month = month;
	sDate.Year = year;

	switch (month) {
	case 2:
		if (year % 4 == 0) {
			if (day > 29) {
				USART_fsend("Niepoprawny format daty.");
				return;
			}
		} else {
			if (day > 28) {
				USART_fsend("Niepoprawny format daty.");
				return;
			}
		}
		break;
	case 4:
	case 6:
	case 9:
	case 11:
		if (day > 30) {
			USART_fsend("Niepoprawny format daty.");
			return;
		}
		break;
	default:
		break;
	}

	switch (weekDay) {
	case 0:
		sDate.WeekDay = RTC_WEEKDAY_SUNDAY;
		break;
	case 1:
		sDate.WeekDay = RTC_WEEKDAY_MONDAY;
		break;
	case 2:
		sDate.WeekDay = RTC_WEEKDAY_TUESDAY;
		break;
	case 3:
		sDate.WeekDay = RTC_WEEKDAY_WEDNESDAY;
		break;
	case 4:
		sDate.WeekDay = RTC_WEEKDAY_THURSDAY;
		break;
	case 5:
		sDate.WeekDay = RTC_WEEKDAY_FRIDAY;
		break;
	case 6:
		sDate.WeekDay = RTC_WEEKDAY_SATURDAY;
		break;
	default:
		sDate.WeekDay = RTC_WEEKDAY_MONDAY;
		break;
	}

	HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
	setAlarm();
}

int compareAlarmValue(RTC_AlarmTypeDef Alarm1, RTC_AlarmTypeDef Alarm2) {
	if (Alarm1.AlarmTime.Hours < Alarm2.AlarmTime.Hours) {
		return 1;
	} else if ((Alarm1.AlarmTime.Hours == Alarm2.AlarmTime.Hours)
			&& (Alarm1.AlarmTime.Minutes < Alarm2.AlarmTime.Minutes)) {
		return 1;
	} else if ((Alarm1.AlarmTime.Hours == Alarm2.AlarmTime.Hours)
			&& (Alarm1.AlarmTime.Minutes == Alarm2.AlarmTime.Minutes)
			&& (Alarm1.AlarmTime.Seconds < Alarm2.AlarmTime.Seconds)) {
		return 1;
	} else {
		return 0;
	}
}

int compareAlarmWithTime(RTC_AlarmTypeDef Alarm) {
	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
	if (sTime.Hours < Alarm.AlarmTime.Hours) {
		return 1;
	} else if ((sTime.Hours == Alarm.AlarmTime.Hours)
			&& (sTime.Minutes < Alarm.AlarmTime.Minutes)) {
		return 1;
	} else if ((sTime.Hours == Alarm.AlarmTime.Hours)
			&& (sTime.Minutes == Alarm.AlarmTime.Minutes)
			&& (sTime.Seconds < Alarm.AlarmTime.Seconds)) {
		return 1;
	} else {
		return 0;
	}
}

void sortAlarmValue(int weekDay) {
	RTC_AlarmTypeDef tempAlarm;
	int tempAlarmActive;
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 2; j++) {
			if (AlarmActive[weekDay][j] < AlarmActive[weekDay][j + 1]) {
				tempAlarm = Alarm[weekDay][j];
				tempAlarmActive = AlarmActive[weekDay][j];
				Alarm[weekDay][j] = Alarm[weekDay][j + 1];
				AlarmActive[weekDay][j] = AlarmActive[weekDay][j + 1];
				Alarm[weekDay][j + 1] = tempAlarm;
				AlarmActive[weekDay][j + 1] = tempAlarmActive;
			} else if (AlarmActive[weekDay][j] == 1
					&& AlarmActive[weekDay][j + 1] == 1) {
				if (compareAlarmValue(Alarm[weekDay][j], Alarm[weekDay][j + 1])
						== 0) {
					tempAlarm = Alarm[weekDay][j];
					tempAlarmActive = AlarmActive[weekDay][j];
					Alarm[weekDay][j] = Alarm[weekDay][j + 1];
					AlarmActive[weekDay][j] = AlarmActive[weekDay][j + 1];
					Alarm[weekDay][j + 1] = tempAlarm;
					AlarmActive[weekDay][j + 1] = tempAlarmActive;
				}
			}
		}
	}
}

int checkIfSameAlarmExist(int hours, int minutes, int seconds, int weekDay) {
	for (int i = 0; i <= 2; i++) {
		if (Alarm[weekDay][i].AlarmTime.Hours == hours
				&& Alarm[weekDay][i].AlarmTime.Minutes == minutes
				&& Alarm[weekDay][i].AlarmTime.Seconds == seconds) {
			return 1;
		}
	}
	return 0;
}

void setAlarmForDay(int weekDay) {
	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
	for (int i = 0; i <= 2; i++) {
		if (AlarmActive[weekDay][i] == 1) {
			if (compareAlarmWithTime(Alarm[weekDay][i]) == 1) {
				sAlarm.AlarmTime.Hours = Alarm[weekDay][i].AlarmTime.Hours;
				sAlarm.AlarmTime.Minutes = Alarm[weekDay][i].AlarmTime.Minutes;
				sAlarm.AlarmTime.Seconds = Alarm[weekDay][i].AlarmTime.Seconds;
				sAlarm.AlarmTime.SubSeconds = 0;
				sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
				sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
				sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
				sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
				sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
				sAlarm.AlarmDateWeekDay = sDate.Date;
				sAlarm.Alarm = RTC_ALARM_A;
				if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN)
						!= HAL_OK) {
					Error_Handler();
				}
				return;
			}
		}
	}
	sAlarm.AlarmTime.Hours = 0;
	sAlarm.AlarmTime.Minutes = 0;
	sAlarm.AlarmTime.Seconds = 0;
	sAlarm.AlarmTime.SubSeconds = 0;
	sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
	sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
	sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
	sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
	switch (sDate.Month) {
	case 2:
		if (sDate.Year % 4 == 0) {
			if (sDate.Date == 29) {
				sAlarm.AlarmDateWeekDay = 1;
			} else {
				sAlarm.AlarmDateWeekDay = sDate.Date + 1;
			}
		} else {
			if (sDate.Date > 28) {
				sAlarm.AlarmDateWeekDay = 1;
			} else {
				sAlarm.AlarmDateWeekDay = sDate.Date + 1;
			}
		}
		break;
	case 4:
	case 6:
	case 9:
	case 11:
		if (sDate.Date == 30) {
			sAlarm.AlarmDateWeekDay = 1;
		} else {
			sAlarm.AlarmDateWeekDay = sDate.Date + 1;
		}
		break;
	default:
		if (sDate.Date == 31) {
			sAlarm.AlarmDateWeekDay = 1;
		} else {
			sAlarm.AlarmDateWeekDay = sDate.Date + 1;
		}
		break;
	}
	sAlarm.Alarm = RTC_ALARM_A;
	if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN) != HAL_OK) {
		Error_Handler();
	}
}

void setAlarm() {
	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

	switch (sDate.WeekDay) {
	case 0x01:
		setAlarmForDay(1);
		break;
	case 0x02:
		setAlarmForDay(2);
		break;
	case 0x03:
		setAlarmForDay(3);
		break;
	case 0x04:
		setAlarmForDay(4);
		break;
	case 0x05:
		setAlarmForDay(5);
		break;
	case 0x06:
		setAlarmForDay(6);
	case 0x07:
		setAlarmForDay(0);
		break;
	default:
		break;
	}

}

void setAlarmValue(int hours, int minutes, int seconds, int weekDay) {
	for (int i = 0; i <= 2; i++) {
		if (AlarmActive[weekDay][i] == 0) {
			if (checkIfSameAlarmExist(hours, minutes, seconds, weekDay) != 1) {
				AlarmActive[weekDay][i] = 1;
				Alarm[weekDay][i].AlarmTime.Hours = hours;
				Alarm[weekDay][i].AlarmTime.Minutes = minutes;
				Alarm[weekDay][i].AlarmTime.Seconds = seconds;
				sortAlarmValue(weekDay);
				setAlarm();
				USART_fsend("Alarm został ustawiony");
				return;
			} else {
				USART_fsend("Już istnieje taki alarm.");
				return;
			}
		}
	}
	USART_fsend("Wszystkie alarmy dla tego dnia są zajete.");
}

void removeAlarm(int weekDay, int slot) {
	if (AlarmActive[weekDay][slot] == 1) {
		AlarmActive[weekDay][slot] = 0;
		if (sAlarm.AlarmTime.Hours == Alarm[weekDay][slot].AlarmTime.Hours
				&& sAlarm.AlarmTime.Minutes
						== Alarm[weekDay][slot].AlarmTime.Minutes
				&& sAlarm.AlarmTime.Seconds
						== Alarm[weekDay][slot].AlarmTime.Seconds) {
			HAL_RTC_DeactivateAlarm(&hrtc, sAlarm.Alarm);
		}
		sortAlarmValue(weekDay);
		setAlarm();
		USART_fsend("Alarm zostal usuniety.");

	} else {
		USART_fsend("Wybrany alarm nie jest ustawiony.");
	}
}

void editAlarm(int weekDay, int slot, int hours, int minutes, int seconds) {
	if (AlarmActive[weekDay][slot] == 1) {
		if (sAlarm.AlarmTime.Hours == Alarm[weekDay][slot].AlarmTime.Hours
				&& sAlarm.AlarmTime.Minutes
						== Alarm[weekDay][slot].AlarmTime.Minutes
				&& sAlarm.AlarmTime.Seconds
						== Alarm[weekDay][slot].AlarmTime.Seconds) {
			HAL_RTC_DeactivateAlarm(&hrtc, sAlarm.Alarm);

		}
		Alarm[weekDay][slot].AlarmTime.Hours = hours;
		Alarm[weekDay][slot].AlarmTime.Minutes = minutes;
		Alarm[weekDay][slot].AlarmTime.Seconds = seconds;
		sortAlarmValue(weekDay);
		setAlarm();
		USART_fsend("Alarm zostal zmodyfikowany.");

	} else {
		USART_fsend("Wybrany alarm nie jest ustawiony.");
	}
}

void showAlarms() {
	for (int i = 0; i <= 6; i++) {
		USART_fsend("%d", i);
		for (int j = 0; j <= 2; j++) {
			if (AlarmActive[i][j] == 1) {
				USART_fsend("%d. %02d'%02d'%02d", j,
						Alarm[i][j].AlarmTime.Hours,
						Alarm[i][j].AlarmTime.Minutes,
						Alarm[i][j].AlarmTime.Seconds);
			} else {
				USART_fsend("%d. Alarm nieaktywny", j);
			}

		}
	}
}

void displayLcdTimeAndDate() {
	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
	LCD_clrScr();
	char LCD_Time[10] = { 0 };
	sprintf(LCD_Time, "%02d:%02d:%02d", sTime.Hours, sTime.Minutes,
			sTime.Seconds);
	char LCD_Date[12] = { 0 };
	sprintf(LCD_Date, "%02d.%02d.20%02d", sDate.Date, sDate.Month,
			sDate.Year);
	LCD_print(LCD_Time, 5, 2);
	LCD_print(LCD_Date, 5, 3);
}

void startCommand(char *command) {
	int hour, minute, second;
	int weekDay;
	int day, month, year;
	int slot;
	if (strcmp("SHOW(ALARMS)", command) == 0) {
		USART_fsend("Wyświetlam alarmy");
		showAlarms();
	} else if (strcmp("SHOW(DATE)", command) == 0) {
		USART_fsend("Pokazuje date");
		showDate();
	} else if (strcmp("SHOW(TIME)", command) == 0) {
		USART_fsend("Pokazuje czas");
		showTime();
	} else if (sscanf(command, "SET_DATE[%d.%d.%d,%d];", &day, &month, &year,
			&weekDay) == 4) {
		if ((day >= 1 && day <= 31) && (month >= 1 && month <= 12)
				&& (year >= 1 && year <= 99)
				&& (weekDay >= 0 && weekDay <= 6)) {
			USART_fsend("Ustawiam date: %d.%d.%d", day, month, year);
			setDate(year, month, day, weekDay);
		} else {
			USART_fsend("Niepoprawny format daty");
		}

	} else if (sscanf(command, "SET_TIME[%d.%d.%d];", &hour, &minute, &second)
			== 3) {
		if ((hour >= 0 && hour <= 23) && (minute >= 0 && minute <= 59)
				&& (second >= 0 && second <= 59)) {
			USART_fsend("Ustawiam czas: %d.%d.%d", hour, minute, second);
			setTime(hour, minute, second);
		} else {
			USART_fsend("Wprowadzono błędny format czasu.");
		}

	} else if (sscanf(command, "SET_ALARM[%d,%d.%d.%d];", &weekDay, &hour,
			&minute, &second) == 4) {
		if (weekDay >= 0 && weekDay <= 6) {
			setAlarmValue(hour, minute, second, weekDay);
		} else {
			USART_fsend("Wprowadzono błędną wartość dnia.");
		}
	} else if (sscanf(command, "REMOVE_ALARM[%d,%d];", &weekDay, &slot) == 2) {
		removeAlarm(weekDay, slot);
	} else if (sscanf(command, "EDIT_ALARM[%d,%d,%d.%d.%d];", &weekDay, &slot,
			&hour, &minute, &second) == 5) {
		editAlarm(weekDay, slot, hour, minute, second);
	} else {
		USART_fsend("Nieprawidłowa komenda");
	}
}

void USART_getline() {
	uint8_t sign = USART_getchar();

	if (sign == 0x23) {
		frame_status = 1;
		frame_idx = 0;
		memset(&FRAME[0], 0, sizeof(FRAME));
		FRAME[frame_idx] = sign;
		frame_idx++;
	} else if (sign == 0x3A) {
		if (frame_status == 1) {
			frame_status = 0;
			FRAME[frame_idx] = sign;
			frame_idx++;
			char checksum_frame[3] = { FRAME[frame_idx - 3],
					FRAME[frame_idx - 2], '\0' };
			int checksum_frame_dec = convertHexToDecimal(checksum_frame);
			FRAME[frame_idx - 3] = '\0';
			memmove(&FRAME[0], &FRAME[1], MAX_SIZE_CMD);
			int checksum_program = check_sum_command(FRAME);
			if (checksum_program == checksum_frame_dec) {
				startCommand(FRAME);
			} else {
				USART_fsend(
						"Suma kontrolna nie jest zgodna z suma kontrolna podawana przez uzytkownika.");
				USART_fsend(
						"Suma kontrolna podana przez uzytkownika wynosi: %02X.",
						checksum_frame_dec);
				USART_fsend(
						"Suma kontrolna wyliczona przez program wynosi: %02X.",
						checksum_program);
			}
		}
	} else {
		FRAME[frame_idx] = sign;
		frame_idx++;
		if (frame_idx > MAX_SIZE_CMD) {
			USART_fsend("Przekroczono dlugosc ramki.");
			frame_status = 0;
			return;
		}
	}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
	LCD_setRST(RST_GPIO_Port, RST_Pin);
	LCD_setCE(CE_GPIO_Port, CE_Pin);
	LCD_setDC(DC_GPIO_Port, DC_Pin);
	LCD_setDIN(DIN_GPIO_Port, DIN_Pin);
	LCD_setCLK(CLK_GPIO_Port, CLK_Pin);
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_RTC_Init();
  MX_USART2_UART_Init();
  MX_TIM10_Init();
  /* USER CODE BEGIN 2 */
	LCD_init();
	HAL_TIM_Base_Start_IT(&htim10);
	HAL_UART_Receive_IT(&huart2, &USART_RX_BUFFER[0], 1);
	USART_fsend("STM32 Start");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	while (1) {
		if (USART_kbhit()) {
			USART_getline();
		}
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  RTC_AlarmTypeDef sAlarm = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
	if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != 0x32F2) {
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 23;
  sTime.Minutes = 59;
  sTime.Seconds = 0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_WEDNESDAY;
  sDate.Month = RTC_MONTH_FEBRUARY;
  sDate.Date = 1;
  sDate.Year = 23;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the Alarm A
  */
  sAlarm.AlarmTime.Hours = 0;
  sAlarm.AlarmTime.Minutes = 0;
  sAlarm.AlarmTime.Seconds = 0;
  sAlarm.AlarmTime.SubSeconds = 0;
  sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
  sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
  sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
  sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
  sAlarm.AlarmDateWeekDay = 1;
  sAlarm.Alarm = RTC_ALARM_A;
  if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the WakeUp
  */
  if (HAL_RTCEx_SetWakeUpTimer(&hrtc, 0, RTC_WAKEUPCLOCK_RTCCLK_DIV16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
		HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x32F2);
	}
  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief TIM10 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM10_Init(void)
{

  /* USER CODE BEGIN TIM10_Init 0 */

  /* USER CODE END TIM10_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM10_Init 1 */

  /* USER CODE END TIM10_Init 1 */
  htim10.Instance = TIM10;
  htim10.Init.Prescaler = 1599;
  htim10.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim10.Init.Period = 9999;
  htim10.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim10.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim10) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim10) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TIMING;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_OC_ConfigChannel(&htim10, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM10_Init 2 */

  /* USER CODE END TIM10_Init 2 */
  HAL_TIM_MspPostInit(&htim10);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 19200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, OUTPUT_Pin|DC_Pin|CE_Pin|RST_Pin
                          |DIN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CLK_GPIO_Port, CLK_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : OUTPUT_Pin DC_Pin CE_Pin RST_Pin
                           DIN_Pin */
  GPIO_InitStruct.Pin = OUTPUT_Pin|DC_Pin|CE_Pin|RST_Pin
                          |DIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : CLK_Pin */
  GPIO_InitStruct.Pin = CLK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CLK_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc) {
	if (sAlarm.AlarmTime.Hours == 0 && sAlarm.AlarmTime.Minutes == 0
			&& sAlarm.AlarmTime.Seconds == 0) {
		USART_fsend("Zmiana dnia");
	} else {
		USART_fsend("ALARM: %02d.%02d.%02d", sAlarm.AlarmTime.Hours,
				sAlarm.AlarmTime.Minutes, sAlarm.AlarmTime.Seconds);
	}

	setAlarm();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM10) {
		displayLcdTimeAndDate();
	}
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1) {
	}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
