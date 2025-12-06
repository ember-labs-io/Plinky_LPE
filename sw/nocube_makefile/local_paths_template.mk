# Rename this file to local_paths.mk and update the paths to reflect your system
# if the arm-none-eabi-* is in your path, then you can leave TOOLCHAIN_LOCATION empty

TOOLCHAIN_LOCATION ?= /Applications/STMicroelectronics/STM32CubeIDE/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.100.202509120712/tools/bin/
STM32_CUBE_PROGRAMMER ?= /Applications/STMicroelectronics/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/MacOs/bin/STM32_Programmer_CLI