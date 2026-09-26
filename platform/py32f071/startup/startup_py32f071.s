.syntax unified
.cpu cortex-m0plus
.thumb

.global g_pfnVectors
.global Reset_Handler
.global Default_Handler

.section .text.Reset_Handler,"ax",%progbits
.type Reset_Handler, %function
Reset_Handler:
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata

1:
    cmp r0, r1
    bcs 2f
    ldr r3, [r2]
    str r3, [r0]
    adds r0, r0, #4
    adds r2, r2, #4
    b 1b

2:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0

3:
    cmp r0, r1
    bcs 4f
    str r2, [r0]
    adds r0, r0, #4
    b 3b

4:
    bl SystemInit
    bl __libc_init_array
    bl main

5:
    b 5b

.size Reset_Handler, .-Reset_Handler

.section .text.Default_Handler,"ax",%progbits
.type Default_Handler, %function
Default_Handler:
6:
    b 6b
.size Default_Handler, .-Default_Handler

.section .isr_vector,"a",%progbits
.type g_pfnVectors, %object
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word 0
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    .word WWDG_IRQHandler
    .word PVD_IRQHandler
    .word RTC_IRQHandler
    .word FLASH_IRQHandler
    .word RCC_CTC_IRQHandler
    .word EXTI0_1_IRQHandler
    .word EXTI2_3_IRQHandler
    .word EXTI4_15_IRQHandler
    .word LCD_IRQHandler
    .word DMA1_Channel1_IRQHandler
    .word DMA1_Channel2_3_IRQHandler
    .word DMA1_Channel4_5_6_7_IRQHandler
    .word ADC_COMP_IRQHandler
    .word TIM1_BRK_UP_TRG_COM_IRQHandler
    .word TIM1_CC_IRQHandler
    .word TIM2_IRQHandler
    .word TIM3_IRQHandler
    .word TIM6_LPTIM1_DAC_IRQHandler
    .word TIM7_IRQHandler
    .word TIM14_IRQHandler
    .word TIM15_IRQHandler
    .word TIM16_IRQHandler
    .word TIM17_IRQHandler
    .word I2C1_IRQHandler
    .word I2C2_IRQHandler
    .word SPI1_IRQHandler
    .word SPI2_IRQHandler
    .word USART1_IRQHandler
    .word USART2_IRQHandler
    .word USART3_IRQHandler
    .word 0
    .word USB_IRQHandler
.size g_pfnVectors, .-g_pfnVectors

.macro weak_handler handler
    .weak \handler
    .thumb_set \handler, Default_Handler
.endm

weak_handler NMI_Handler
weak_handler HardFault_Handler
weak_handler SVC_Handler
weak_handler PendSV_Handler
weak_handler SysTick_Handler
weak_handler WWDG_IRQHandler
weak_handler PVD_IRQHandler
weak_handler RTC_IRQHandler
weak_handler FLASH_IRQHandler
weak_handler RCC_CTC_IRQHandler
weak_handler EXTI0_1_IRQHandler
weak_handler EXTI2_3_IRQHandler
weak_handler EXTI4_15_IRQHandler
weak_handler LCD_IRQHandler
weak_handler DMA1_Channel1_IRQHandler
weak_handler DMA1_Channel2_3_IRQHandler
weak_handler DMA1_Channel4_5_6_7_IRQHandler
weak_handler ADC_COMP_IRQHandler
weak_handler TIM1_BRK_UP_TRG_COM_IRQHandler
weak_handler TIM1_CC_IRQHandler
weak_handler TIM2_IRQHandler
weak_handler TIM3_IRQHandler
weak_handler TIM6_LPTIM1_DAC_IRQHandler
weak_handler TIM7_IRQHandler
weak_handler TIM14_IRQHandler
weak_handler TIM15_IRQHandler
weak_handler TIM16_IRQHandler
weak_handler TIM17_IRQHandler
weak_handler I2C1_IRQHandler
weak_handler I2C2_IRQHandler
weak_handler SPI1_IRQHandler
weak_handler SPI2_IRQHandler
weak_handler USART1_IRQHandler
weak_handler USART2_IRQHandler
weak_handler USART3_IRQHandler
weak_handler USB_IRQHandler

.section .note.GNU-stack,"",%progbits
