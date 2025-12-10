/*
 * Modulino_Knob.c
 *
 *  Created on: Nov 10, 2025
 *      Author: andreagiuri
 */


#include "Modulino_Knob.h"
#include "stdio.h"

uint8_t KNOB_ADDR = 0x3A<<1;


HAL_StatusTypeDef Knob_Set_Position(uint16_t set){
	HAL_StatusTypeDef ret;
	uint16_t negset = 0 - set;
	uint8_t send[4];
	send[0] = (uint8_t)(negset);
	send[1] = (uint8_t)(negset >> 8);
	send[2] = 0;
	send[3] = 0;

	ret = HAL_I2C_Master_Transmit(&KNOB_I2C_PORT, KNOB_ADDR, send, 4,
				100);
	if (ret != HAL_OK) {
		return HAL_ERROR;
	}
}


uint16_t Knob_Read_Position(void){
	uint8_t reading[4];
	uint16_t encoder;
	HAL_I2C_Master_Receive(&KNOB_I2C_PORT, KNOB_ADDR, &reading, 4, 100);
	encoder = (reading[2]<<8) + reading[1];

	return encoder;
}


uint8_t Knob_Read_Switch(void){
	uint8_t reading[4];

	HAL_I2C_Master_Receive(&KNOB_I2C_PORT, KNOB_ADDR, &reading, 4, 100);

	return reading[3];
}
