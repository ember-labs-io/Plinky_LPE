# Toolchain and programmer locations - create this for your system from local_paths_template.mk
-include sw/build/local_paths.mk

# You can also set this from the commandline:
# make TOOLCHAIN_LOCATION=/path/to/toolchain STM32_CUBE_PROGRAMMER=/path/to/STM32_Programmer_CLI


# Compiler and linker with full paths
CC = $(TOOLCHAIN_LOCATION)arm-none-eabi-gcc
OBJCOPY = $(TOOLCHAIN_LOCATION)arm-none-eabi-objcopy
OBJDUMP = $(TOOLCHAIN_LOCATION)arm-none-eabi-objdump
SIZE = $(TOOLCHAIN_LOCATION)arm-none-eabi-size

# Output files
# Set default build type if not specified
BUILD_TYPE ?= RELEASE

BUILD_DIR := sw/build/$(BUILD_TYPE)
$(shell mkdir -p $(BUILD_DIR))

TARGET = $(BUILD_DIR)/plinkyblack.elf
BIN = $(BUILD_DIR)/plinkyblack.bin
HEX = $(BUILD_DIR)/plinkyblack.hex
LIST = $(BUILD_DIR)/plinkyblack.list
MAP = $(BUILD_DIR)/plinkyblack.map

# Source files
SRCS = \
	sw/Core/Src/plinky/plinky.c \
	sw/Core/Src/plinky/data/tables.c \
	sw/Core/Src/plinky/hardware/accelerometer.c \
	sw/Core/Src/plinky/hardware/adc_dac.c \
	sw/Core/Src/plinky/hardware/codec.c \
	sw/Core/Src/plinky/hardware/encoder.c \
	sw/Core/Src/plinky/hardware/expander.c \
	sw/Core/Src/plinky/hardware/leds.c \
	sw/Core/Src/plinky/hardware/midi.c \
	sw/Core/Src/plinky/hardware/memory.c \
	sw/Core/Src/plinky/hardware/spi.c \
	sw/Core/Src/plinky/hardware/touchstrips.c \
	sw/Core/Src/plinky/synth/arp.c \
	sw/Core/Src/plinky/synth/audio.c \
	sw/Core/Src/plinky/synth/lfos.c \
	sw/Core/Src/plinky/synth/params.c \
	sw/Core/Src/plinky/synth/sampler.c \
	sw/Core/Src/plinky/synth/sequencer.c \
	sw/Core/Src/plinky/synth/synth.c \
	sw/Core/Src/plinky/synth/time.c \
	sw/Core/Src/plinky/ui/led_viz.c \
	sw/Core/Src/plinky/ui/oled_viz.c \
	sw/Core/Src/plinky/ui/pad_actions.c \
	sw/Core/Src/plinky/ui/settings_menu.c \
	sw/Core/Src/plinky/usb/tinyusb/src/tusb.c \
	sw/Core/Src/plinky/usb/tinyusb/src/usb_descriptors.c \
	sw/Core/Src/plinky/usb/tinyusb/src/usbmidi.c \
	sw/Core/Src/plinky/usb/tinyusb/src/class/midi/midi_device.c \
	sw/Core/Src/plinky/usb/tinyusb/src/class/vendor/vendor_device.c \
	sw/Core/Src/plinky/usb/tinyusb/src/common/tusb_fifo.c \
	sw/Core/Src/plinky/usb/tinyusb/src/device/usbd.c \
	sw/Core/Src/plinky/usb/tinyusb/src/device/usbd_control.c \
	sw/Core/Src/plinky/usb/tinyusb/src/portable/st/synopsys/dcd_synopsys.c \
	sw/Core/Src/plinky/usb/usb.c \
	sw/Core/Src/plinky/usb/web_editor.c \
	sw/Core/Src/plinky/gfx/gfx.c \
	sw/Core/Src/plinky/gfx/oled/oled.c \
	sw/Core/Src/main.c \
	sw/Core/Src/lis2dh12_reg.c \
	sw/Core/Src/stm32l4xx_hal_msp.c \
	sw/Core/Src/stm32l4xx_it.c \
	sw/Core/Src/syscalls.c \
	sw/Core/Src/sysmem.c \
	sw/Core/Src/system_stm32l4xx.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_adc.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_adc_ex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_cortex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dac.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dma.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_gpio.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c_ex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pcd.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pcd_ex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr_ex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc_ex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_sai.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_spi.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_tim.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_tim_ex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_tsc.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart_ex.c \
	sw/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_ll_usb.c

OBJS := $(SRCS:sw/%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)


# Base CFLAGS and LDFLAGS
CFLAGS_COMMON = -mcpu=cortex-m4 \
    -std=gnu11 \
    -DUSE_HAL_DRIVER \
    -DSTM32L476xx \
    -ffunction-sections \
    -fdata-sections \
    -Wall \
	-Wenum-conversion \
    -fstack-usage \
    --specs=nano.specs \
    -mfpu=fpv4-sp-d16 \
    -mfloat-abi=hard \
    -mthumb

LDFLAGS_COMMON = -mcpu=cortex-m4 \
    -T"sw/build/STM32L476VGTX_FLASH.ld" \
    --specs=nosys.specs \
    -Wl,-Map="$(MAP)" \
    -Wl,--gc-sections \
    -static \
    --specs=nano.specs \
    -mfpu=fpv4-sp-d16 \
    -mfloat-abi=hard \
    -mthumb \
    -Wl,--start-group -lc -lm -Wl,--end-group

# Flags for release build
ifeq ($(BUILD_TYPE), RELEASE)
CFLAGS = $(CFLAGS_COMMON) -O3 -DNDEBUG
LDFLAGS = $(LDFLAGS_COMMON)
endif

# Flags for debug build
ifeq ($(BUILD_TYPE), DEBUG)
CFLAGS = $(CFLAGS_COMMON) -Og -g3 -D_DEBUG -DDEBUG -gdwarf-2
LDFLAGS = $(LDFLAGS_COMMON)
endif

# debug flags
# CFLAGS += -DDEBUG_LOG
# CFLAGS += -DTIME_LOGGING
# CFLAGS += -DFPS_WINDOW

# Print build type
$(info Building in $(BUILD_TYPE) mode)

INCLUDES = -Isw/Drivers/CMSIS/Include \
	    -Isw/Core/Src/plinky/usb/tinyusb/src \
	    -Isw/Core/Inc \
	    -Isw/Drivers/CMSIS/Device/ST/STM32L4xx/Include \
	    -Isw/Drivers/STM32L4xx_HAL_Driver/Inc \
	    -Isw/Drivers/STM32L4xx_HAL_Driver/Inc/Legacy \
		-Isw/Core/Src/plinky \
		-Isw/Core/Src/rebuild

all: $(TARGET) $(BIN) $(HEX) $(LIST) size

# Special rule for startup assembly file
$(BUILD_DIR)/Core/Startup/startup_stm32l476vgtx.o: sw/Core/Startup/startup_stm32l476vgtx.s
	@echo "AS $<"
	@mkdir -p $(dir $@)
	@$(CC) -mcpu=cortex-m4 -c -x assembler-with-cpp -MMD -MP \
	--specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o $@ $<

$(BUILD_DIR)/%.o: sw/%.c
	@echo "CC $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -c $< -o $@

# $(OBJS): %.o: sw/%.c
# 	@echo "CC $<"
# 	@mkdir -p $(dir $@)
# 	@$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -c $< -o $@

# Linking
$(TARGET): $(OBJS) $(BUILD_DIR)/Core/Startup/startup_stm32l476vgtx.o
	@echo "LD $@"
	@$(CC) $(OBJS) $(BUILD_DIR)/Core/Startup/startup_stm32l476vgtx.o $(LDFLAGS) -o $@

# Post-build steps
$(BIN): $(TARGET)
	@echo "Creating binary $@"
	@$(OBJCOPY) -O binary $< $@

$(HEX): $(TARGET)
	@echo "Creating hex $@"
	@$(OBJCOPY) -O ihex $< $@

$(LIST): $(TARGET)
	@echo "Creating listing $@"
	@$(OBJDUMP) -h -S $< > $@

program: $(HEX)
ifeq ($(STM32_CUBE_PROGRAMMER),)
	$(error STM32_CUBE_PROGRAMMER is not set. Please set the path to STM32_Programmer_CLI)
else
	@echo "Flashing $<"
	@$(STM32_CUBE_PROGRAMMER) -c port=SWD -w $< -v -rst
endif

size: $(TARGET)
	@echo "Size of modules:"
	@$(SIZE) $<

clean:
	rm -rf $(BUILD_DIR) 
	
toolchain-info:
	@echo "Current toolchain location: $(TOOLCHAIN_LOCATION)"
	@echo "Update TOOLCHAIN_LOCATION in Makefile if this is incorrect"

.PHONY: all clean size toolchain-info

-include $(DEPS)
