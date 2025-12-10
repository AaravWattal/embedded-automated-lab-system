/*
 * Modulino_Knob.h
 *
 *  Created on: Nov 10, 2025
 *      Author: andreagiuri
 */

#ifndef INC_MODULINO_KNOB_H_
#define INC_MODULINO_KNOB_H_

#include "main.h"
#include "stm32l4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;
#define KNOB_I2C_PORT hi2c1

HAL_StatusTypeDef Knob_Set_Position(uint16_t set);
uint16_t Knob_Read_Position(void);
uint8_t Knob_Read_Switch(void);



#endif /* INC_MODULINO_KNOB_H_ */
