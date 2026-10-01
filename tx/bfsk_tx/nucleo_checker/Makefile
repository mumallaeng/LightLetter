# Windows GNU Make. ARM_Lab과 동일한 make / make run 흐름.
.DEFAULT_GOAL := all
SHELL := cmd.exe
.SHELLFLAGS := /C
.DELETE_ON_ERROR:

TOOL_DIR ?= C:/arm-gnu-toolchain-15.2.rel1-mingw-w64-i686-arm-none-eabi
CUBE_ROOT ?= $(subst \,/,$(USERPROFILE))/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3
PROGRAMMER ?= C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe
HOST_CC ?= gcc
STLINK_SN ?=

TARGET := Nucleo_tx_test
BUILD := build
ELF := $(BUILD)/$(TARGET).elf
HAL_ROOT := $(CUBE_ROOT)/Drivers/STM32F4xx_HAL_Driver
CMSIS_ROOT := $(CUBE_ROOT)/Drivers/CMSIS
DEVICE_ROOT := $(CMSIS_ROOT)/Device/ST/STM32F4xx

CC := "$(TOOL_DIR)/bin/arm-none-eabi-gcc.exe"
OBJCOPY := "$(TOOL_DIR)/bin/arm-none-eabi-objcopy.exe"
SIZE := "$(TOOL_DIR)/bin/arm-none-eabi-size.exe"
CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CPPFLAGS := -DSTM32F411xE -DUSE_HAL_DRIVER -Iconfig -Isrc \
    -I"$(HAL_ROOT)/Inc" -I"$(CMSIS_ROOT)/Include" -I"$(DEVICE_ROOT)/Include"
CFLAGS := $(CPU_FLAGS) -std=gnu11 -Os -g3 -Wall -Wextra \
    -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS := $(CPU_FLAGS) --specs=nano.specs --specs=nosys.specs \
    -Tconfig/stm32f411re.ld -Wl,--gc-sections \
    -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage

HAL_MODULES := hal hal_rcc hal_rcc_ex hal_gpio hal_dma hal_cortex \
    hal_flash hal_flash_ex hal_pwr hal_pwr_ex hal_tim hal_tim_ex hal_uart
C_SOURCES := src/main.c src/drv/bfsk_rx.c src/hal/nucleo_board.c \
    $(DEVICE_ROOT)/Source/Templates/system_stm32f4xx.c \
    $(addprefix $(HAL_ROOT)/Src/stm32f4xx_,$(addsuffix .c,$(HAL_MODULES)))
STARTUP := $(DEVICE_ROOT)/Source/Templates/gcc/startup_stm32f411xe.s
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o))) \
    $(BUILD)/startup_stm32f411xe.o
vpath %.c $(sort $(dir $(C_SOURCES)))

.PHONY: all size run flash test clean help
all: $(ELF) $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex size

$(BUILD):
	@if not exist "$(BUILD)" mkdir "$(BUILD)"

$(BUILD)/%.o: %.c Makefile config/stm32f4xx_hal_conf.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD)/startup_stm32f411xe.o: $(STARTUP) Makefile | $(BUILD)
	$(CC) $(CPU_FLAGS) -g3 -c "$<" -o "$@"

$(ELF): $(OBJECTS) config/stm32f411re.ld Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o "$@"

$(BUILD)/$(TARGET).bin: $(ELF)
	$(OBJCOPY) -O binary "$<" "$@"

$(BUILD)/$(TARGET).hex: $(ELF)
	$(OBJCOPY) -O ihex "$<" "$@"

size: $(ELF)
	$(SIZE) "$<"

# 사용자가 make run/flash를 실행할 때만 보드 펌웨어를 교체한다.
run: $(ELF)
	"$(PROGRAMMER)" -c port=SWD $(if $(STLINK_SN),sn=$(STLINK_SN)) -w "$(ELF)" -v -rst -q

flash: run

$(BUILD)/test_bfsk_rx.exe: tests/test_bfsk_rx.c src/drv/bfsk_rx.c src/drv/bfsk_rx.h Makefile | $(BUILD)
	"$(HOST_CC)" -std=c11 -Wall -Wextra -Werror -Isrc/drv \
	    tests/test_bfsk_rx.c src/drv/bfsk_rx.c -o "$@"

test: $(BUILD)/test_bfsk_rx.exe
	"$(subst /,\,$<)"

# 한 PowerShell 안에서 build 바로 아래의 생성 파일만 삭제한다(재귀 삭제 없음).
clean:
	powershell.exe -NoProfile -Command "if (Test-Path -LiteralPath 'build') { Get-ChildItem -LiteralPath 'build' -File | Where-Object { $$_.Extension -in '.o','.d','.elf','.bin','.hex','.map','.log','.exe' } | Remove-Item -Force }"

help:
	@echo make         : build ELF, BIN, HEX
	@echo make run     : build ELF, program/verify via ST-LINK SWD, reset
	@echo make test    : run host decoder tests
	@echo make clean   : remove generated files in build
	@echo Overrides    : TOOL_DIR, CUBE_ROOT, PROGRAMMER, HOST_CC, STLINK_SN

-include $(OBJECTS:.o=.d)
