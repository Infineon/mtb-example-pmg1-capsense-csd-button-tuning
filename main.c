/******************************************************************************
* File Name: main.c
*
* Description: This is the source code for the PMG1 MCU CapSense CSD Button
*              tuning code example for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2021-2023, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include <stdio.h>
#include <inttypes.h>
#include "cy_pdl.h"
#include "cybsp.h"
#include "cycfg.h"
#include "cycfg_capsense.h"

/*******************************************************************************
* Macros
*******************************************************************************/
/* CY assert failure */
#define CY_ASSERT_FAILED          (0u)

/* EZI2C interrupt priority must be higher than CapSense interrupt priority */
#define EZI2C_INTR_PRIORITY       (2u)

/* Capsense interrupt priority */
#define CAPSENSE_INTR_PRIORITY    (3u)

/* Debug print macro to enable UART print */
#define DEBUG_PRINT               (0u)

/* No touch */
#define NO_BUTTON_TOUCH           (0u)

/*******************************************************************************
* Global Definitions
*******************************************************************************/
/* Structure for EZI2C context */
cy_stc_scb_ezi2c_context_t ezi2c_context;

/* EZI2C interrupt configuration structure */
const cy_stc_sysint_t ezi2c_intr_config =
{
    .intrSrc = CYBSP_EZI2C_IRQ,
    .intrPriority = EZI2C_INTR_PRIORITY,
};

/* CapSense interrupt configuration */
const cy_stc_sysint_t capsense_interrupt_config =
{
    .intrSrc = CYBSP_CSD_IRQ,
    .intrPriority = CAPSENSE_INTR_PRIORITY,
};

#if CY_CAPSENSE_BIST_EN
/* Variables to hold sensor parasitic capacitances for Button 0 & Button 1 */
uint32_t button_0_sensor_cp = 0, button_1_sensor_cp = 0;

/* BIST status variable */
cy_en_capsense_bist_status_t cp_0_status, cp_1_status;
#endif /* CY_CAPSENSE_BIST_EN */

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
/* Capsense ISR function */
static void capsense_isr(void);

/* EZI2C ISR function */
static void ezi2c_isr(void);

#if CY_CAPSENSE_BIST_EN
static void measure_sensor_cp(void);
#endif /* CY_CAPSENSE_BIST_EN */

#if DEBUG_PRINT
/* Structure for UART context */
cy_stc_scb_uart_context_t CYBSP_UART_context;

/* Variable used for tracking the print status */
volatile bool ENTER_LOOP = true;

/*******************************************************************************
* Function Name: check_status
********************************************************************************
* Summary:
*  Prints the error message.
*
* Parameters:
*  error_msg - message to print if any error encountered.
*  status - status obtained after evaluation.
*
* Return:
*  void
*
*******************************************************************************/
void check_status(char *message, cy_rslt_t status)
{
    char error_msg[64];

    sprintf(error_msg, "Error Code: 0x%08" PRIX32 "\n", status);

    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n=====================================================\r\n");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\nFAIL: ");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, message);
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, error_msg);
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n=====================================================\r\n");
}
#endif

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  System entrance point. This function performs
*  1. Initial setup of device
*  2. Initialize EZI2C
*  3. Initialize CapSense
*  4. Initialize tuner communication
*  5. Perform Cp measurement if Built-in Self test (BIST) is enabled
*  6. Scan touch input continuously
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    /* Basic result status variable */
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* EZI2C status variable */
    cy_en_scb_ezi2c_status_t ezi2c_result = CY_SCB_EZI2C_SUCCESS;

    /* Capsense status variable */
    cy_capsense_status_t cap_result = CY_CAPSENSE_STATUS_SUCCESS;

    /* SysInt status variable */
    cy_en_sysint_status_t intr_result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

#if DEBUG_PRINT
    /* Configure and enable the UART peripheral */
    Cy_SCB_UART_Init(CYBSP_UART_HW, &CYBSP_UART_config, &CYBSP_UART_context);
    Cy_SCB_UART_Enable(CYBSP_UART_HW);

    /* Sequence to clear screen */
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\x1b[2J\x1b[;H");

    /* Print "CapsenseTM CSD Button Tuning " */
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "****************** ");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "PMG1 MCU: CapsenseTM CSD Button Tuning");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "****************** \r\n\n");
#endif

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize the EZI2C firmware module */
    ezi2c_result = Cy_SCB_EZI2C_Init(CYBSP_EZI2C_HW, &CYBSP_EZI2C_config, &ezi2c_context);

    if (ezi2c_result != CY_SCB_EZI2C_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_SCB_EZI2C_Init failed with error code", ezi2c_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    intr_result = Cy_SysInt_Init(&ezi2c_intr_config, ezi2c_isr);

    if (intr_result != CY_SYSINT_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_SysInt_Init failed with error code", intr_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable EZI2C interrupt */
    NVIC_EnableIRQ(ezi2c_intr_config.intrSrc);

    /* Set the CapSense data structure as the I2C buffer to be exposed to the
     * master on primary slave address interface. Any I2C host tools such as
     * the Tuner or the Bridge Control Panel can read this buffer but you can
     * connect only one tool at a time.
     */
    Cy_SCB_EZI2C_SetBuffer1(CYBSP_EZI2C_HW, (uint8_t *)&cy_capsense_tuner,
                            sizeof(cy_capsense_tuner), sizeof(cy_capsense_tuner),
                            &ezi2c_context);

    /* Enables the SCB block for the EZI2C operation. */
    Cy_SCB_EZI2C_Enable(CYBSP_EZI2C_HW);

    /* Capture the CSD HW block and initialize it to the default state. */
    cap_result = Cy_CapSense_Init(&cy_capsense_context);

    if (cap_result != CY_CAPSENSE_STATUS_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_CapSense_Init failed with error code", cap_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Initialize CapSense interrupt */
    intr_result = Cy_SysInt_Init(&capsense_interrupt_config, capsense_isr);

    if (intr_result != CY_SYSINT_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_SysInt_Init failed with error code", intr_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Clear the pending interrupt */
    NVIC_ClearPendingIRQ(capsense_interrupt_config.intrSrc);

    /* Enable the Capsense interrupt */
    NVIC_EnableIRQ(capsense_interrupt_config.intrSrc);

    /* Initialize the CapSense firmware modules. */
    cap_result = Cy_CapSense_Enable(&cy_capsense_context);

    if (cap_result != CY_CAPSENSE_STATUS_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_CapSense_Enable failed with error code", cap_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Start the first scan */
    cap_result = Cy_CapSense_ScanAllWidgets(&cy_capsense_context);

    if (cap_result != CY_CAPSENSE_STATUS_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_CapSense_ScanAllWidgets failed with error code", cap_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    for (;;)
    {
        if(CY_CAPSENSE_BUSY != Cy_CapSense_IsBusy(&cy_capsense_context))
        {
            /* Process all widgets */
            Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);

            /* Turning Button0 ON/OFF based on button press */
            if(NO_BUTTON_TOUCH != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_BUTTON0_WDGT_ID, &cy_capsense_context))
            {
                Cy_GPIO_Write(CYBSP_LED_BTN0_PORT, CYBSP_LED_BTN0_NUM, CYBSP_LED_STATE_ON);
            }
            else
            {
                Cy_GPIO_Write(CYBSP_LED_BTN0_PORT, CYBSP_LED_BTN0_NUM, CYBSP_LED_STATE_OFF);
            }

            /* Turning Button1 ON/OFF based on button press */
            if(NO_BUTTON_TOUCH != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_BUTTON1_WDGT_ID, &cy_capsense_context))
            {
                Cy_GPIO_Write(CYBSP_LED_BTN1_PORT, CYBSP_LED_BTN1_NUM, CYBSP_LED_STATE_ON);
            }
            else
            {
                Cy_GPIO_Write(CYBSP_LED_BTN1_PORT, CYBSP_LED_BTN1_NUM, CYBSP_LED_STATE_OFF);
            }

            /* Establishes synchronized communication with the CapSense Tuner tool */
            Cy_CapSense_RunTuner(&cy_capsense_context);

#if CY_CAPSENSE_BIST_EN
            /* Measure the self capacitance of sensor electrode using BIST */
            measure_sensor_cp();
#endif /* CY_CAPSENSE_BIST_EN */

            /* Start the next scan */
            Cy_CapSense_ScanAllWidgets(&cy_capsense_context);
        }

#if DEBUG_PRINT
        if (ENTER_LOOP)
        {
            Cy_SCB_UART_PutString(CYBSP_UART_HW, "Entered for loop\r\n");
            ENTER_LOOP = false;
        }
#endif
    }
}

/*******************************************************************************
* Function Name: capsense_isr
********************************************************************************
* Summary:
*  Wrapper function for handling interrupts from CapSense block.
*
* Parameters:
*   void
*
* Return:
*  void
*
*******************************************************************************/
static void capsense_isr(void)
{
    Cy_CapSense_InterruptHandler(CYBSP_CSD_HW, &cy_capsense_context);
}

/*******************************************************************************
* Function Name: ezi2c_isr
********************************************************************************
* Summary:
*  Wrapper function for handling interrupts from EZI2C block.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void ezi2c_isr(void)
{
    Cy_SCB_EZI2C_Interrupt(CYBSP_EZI2C_HW, &ezi2c_context);
}

#if CY_CAPSENSE_BIST_EN

/*******************************************************************************
* Function Name: measure_sensor_cp
********************************************************************************
* Summary:
*  Measures the self capacitance of the sensor electrode (Cp) in Femto Farad and
*  stores its value in the variable button_0_sensor_cp for Button 0 and button_1_sensor_cp for
*  Button 1.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void measure_sensor_cp(void)
{
    /* Measure the self capacitance of sensor electrode for Button 0 Widget */
    cp_0_status = Cy_CapSense_MeasureCapacitanceSensor(CY_CAPSENSE_BUTTON0_WDGT_ID,
                                                  CY_CAPSENSE_BUTTON0_SNS0_ID,
                                             &button_0_sensor_cp, &cy_capsense_context);


    /* Measure the self capacitance of sensor electrode for Button 1 Widget */
    cp_1_status = Cy_CapSense_MeasureCapacitanceSensor(CY_CAPSENSE_BUTTON1_WDGT_ID,
                                                  CY_CAPSENSE_BUTTON1_SNS0_ID,
                                             &button_1_sensor_cp, &cy_capsense_context);
}
#endif /* CY_CAPSENSE_BIST_EN */

/* [] END OF FILE */
