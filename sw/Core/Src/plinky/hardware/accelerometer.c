#include "accelerometer.h"
#include "../../lis2dh12_reg.h"
#include "synth/params.h"

// https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lis2dh12_STdC/example/lis2dh12_read_data_polling.c

extern I2C_HandleTypeDef hi2c2;

#define I2C_TIMEOUT 20

static int32_t platform_write(void* handle, uint8_t reg, uint8_t* bufp, uint16_t len) {
	/* Write multiple command */
	reg |= 0x80;
	HAL_I2C_Mem_Write(handle, LIS2DH12_I2C_ADD_L, reg, I2C_MEMADD_SIZE_8BIT, bufp, len, I2C_TIMEOUT);
	return 0;
}

static int32_t platform_read(void* handle, uint8_t reg, uint8_t* bufp, uint16_t len) {
	/* Read multiple command */
	reg |= 0x80;
	HAL_I2C_Mem_Read(handle, LIS2DH12_I2C_ADD_L, reg, I2C_MEMADD_SIZE_8BIT, bufp, len, I2C_TIMEOUT);
	return 0;
}

static stmdev_ctx_t accelerometer = {.write_reg = platform_write, .read_reg = platform_read, .handle = &hi2c2};
static s16 accel_raw[3];
static float accel_lpf[2];
static float accel_smooth[2];

float accel_get_axis(bool y_axis) {
	return accel_smooth[(u8)y_axis] - accel_lpf[(u8)y_axis];
}

void accel_init(void) {
	HAL_Delay(8);
	u8 whoamI = 0;
	lis2dh12_device_id_get(&accelerometer, &whoamI);
	if (whoamI != LIS2DH12_ID) {
		accelerometer.handle = 0;
	}
	else {
		lis2dh12_block_data_update_set(&accelerometer, PROPERTY_ENABLE);
		lis2dh12_data_rate_set(&accelerometer, LIS2DH12_ODR_100Hz);
		lis2dh12_full_scale_set(&accelerometer, LIS2DH12_2g);
		lis2dh12_temperature_meas_set(&accelerometer, LIS2DH12_TEMP_DISABLE);
		lis2dh12_operating_mode_set(&accelerometer, LIS2DH12_HR_12bit);
	}
}

void accel_read(void) {
	if (!accelerometer.handle)
		return;
	lis2dh12_reg_t reg;
	lis2dh12_xl_data_ready_get(&accelerometer, &reg.byte);
	if (!reg.byte)
		return;
	s16 tmp[3] = {0, 0, 0};
	lis2dh12_acceleration_raw_get(&accelerometer, tmp);
	accel_raw[0] = tmp[0];
	accel_raw[1] = tmp[1];
	accel_raw[2] = tmp[2];
}

void accel_tick(void) {
	static u16 accel_counter;
	// full sensitivity equals 200% scaling
	float accel_sens_f = 2 * param_val(P_ACCEL_SENS) / 65536.f;
	// detect inverted sensor
	bool axis_swap = accel_raw[2] > 4000;
	for (u8 axis_id = 0; axis_id < 2; ++axis_id) {
		float f = accel_raw[axis_id ^ axis_swap] / (float)(1 << 14) * accel_sens_f;
		if (!axis_id) {
			if (!axis_swap)
				f = -f; // reverse x
		}
		else if (accel_sens_f < 0)
			f = -f; // reverse y if accel sens negative
		accel_lpf[axis_id] += (f - accel_lpf[axis_id]) * 0.0001f;
		accel_smooth[axis_id] += (f - accel_smooth[axis_id]) * 0.1f;
		if (accel_counter < 1000) {
			accel_lpf[axis_id] = accel_smooth[axis_id] = f;
			accel_counter++;
		}
	}
}