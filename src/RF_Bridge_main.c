//=========================================================
// src/RF_Bridge_2_main.c: generated by Hardware Configurator
//
// This file will be updated when saving a document.
// leave the sections inside the "$[...]" comment tags alone
// or they will be overwritten!!
//=========================================================

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <SI_EFM8BB1_Register_Enums.h>                  // SFR declarations
#include "Globals.h"
#include "InitDevice.h"
#include "uart_0.h"
#include "pca_0.h"
#include "uart.h"
#include "RF_Handling.h"
#include "RF_Protocols.h"
// $[Generated Includes]
// [Generated Includes]$

SI_SEGMENT_VARIABLE(uart_state, uart_state_t, SI_SEG_XDATA) = IDLE;
SI_SEGMENT_VARIABLE(uart_command, uart_command_t, SI_SEG_XDATA) = NONE;

//-----------------------------------------------------------------------------
// SiLabs_Startup() Routine
// ----------------------------------------------------------------------------
// This function is called immediately after reset, before the initialization
// code is run in SILABS_STARTUP.A51 (which runs before main() ). This is a
// useful place to disable the watchdog timer, which is enable by default
// and may trigger before main() in some instances.
//-----------------------------------------------------------------------------
void SiLabs_Startup (void)
{

}

void StartRFTransmit(void)
{
	actual_bit_of_byte = 0x08;
	actual_bit_of_byte--;
	actual_bit = 1;

	// set first bit to be in sync when PCA0 is starting
	SetPCA0DutyCylce();

	// make RF sync pulse
	SendRF_SYNC();
	PCA0CN0_CR = PCA0CN0_CR__RUN;
}

//-----------------------------------------------------------------------------
// main() Routine
// ----------------------------------------------------------------------------
int main (void)
{
	// Call hardware initialization routine
	enter_DefaultMode_from_RESET();

	// enter default state
	LED = LED_OFF;
	BUZZER = BUZZER_OFF;

	T_DATA = 1;

	// enable UART
	UART0_init(UART0_RX_ENABLE, UART0_WIDTH_8, UART0_MULTIPROC_DISABLE);

	// start sniffing if enabled by default
	if (Sniffing)
	{
		// set desired RF protocol PT2260
		desired_rf_protocol = PT2260_IDENTIFIER;
		uart_command = RF_CODE_RFIN;
		PCA0_DoSniffing(RF_CODE_RFIN);
	}
	else
		PCA0_StopSniffing();

	// enable global interrupts
	IE_EA = 1;

	while (1)
	{
		/*------------------------------------------
		 * check if something got received by UART
		 ------------------------------------------*/
		unsigned int rxdata;
		uint8_t len;
		uint8_t position;
		uint8_t protocol_index;

		rxdata = uart_getc();

		if (rxdata != UART_NO_DATA)
		{
			// state machine for UART
			switch(uart_state)
			{
				// check if UART_SYNC_INIT got received
				case IDLE:
					if ((rxdata & 0xFF) == RF_CODE_START)
						uart_state = SYNC_INIT;
					break;

				// sync byte got received, read command
				case SYNC_INIT:
					uart_command = rxdata & 0xFF;
					uart_state = SYNC_FINISH;

					// check if some data needs to be received
					switch(uart_command)
					{
						case RF_CODE_LEARN:
							InitTimer_ms(1, 50);
							BUZZER = BUZZER_ON;
							// wait until timer has finished
							WaitTimerFinsihed();
							BUZZER = BUZZER_OFF;

							// set desired RF protocol PT2260
							desired_rf_protocol = PT2260_IDENTIFIER;
							PCA0_DoSniffing(RF_CODE_LEARN);

							// start timeout timer
							InitTimer_ms(1, 30000);
							break;
						case RF_CODE_RFOUT:
							uart_state = RECEIVING;
							len = 9;
							break;
						case RF_CODE_SNIFFING_ON:
							desired_rf_protocol = UNKNOWN_IDENTIFIER;
							PCA0_DoSniffing(RF_CODE_SNIFFING_ON);
							break;
						case RF_CODE_SNIFFING_OFF:
							PCA0_StopSniffing();
							sniffing_is_on = false;
							break;
						case RF_CODE_TRANSMIT_DATA_NEW:
							uart_state = RECEIVE_LEN;
							break;

						// unknown command
						default:
							uart_command = NONE;
							uart_state = IDLE;
							break;
					}
					break;

				// Receiving UART data length
				case RECEIVE_LEN:
					position = 0;
					len = rxdata & 0xFF;
					if (len > 0)
						uart_state = RECEIVING;
					else
						uart_state = SYNC_FINISH;
					break;

				// Receiving UART data
				case RECEIVING:
					RF_DATA[position] = rxdata & 0xFF;
					position++;

					if (position == len)
						uart_state = SYNC_FINISH;
					break;

				// wait and check for UART_SYNC_END
				case SYNC_FINISH:
					if ((rxdata & 0xFF) == RF_CODE_STOP)
					{
						uart_state = IDLE;
						// send acknowledge
						uart_put_command(RF_CODE_ACK);
					}
					break;
			}
		}

		/*------------------------------------------
		 * check command byte
		 ------------------------------------------*/
		switch(uart_command)
		{
			// do original learning
			case RF_CODE_LEARN:
				// check if a RF signal got decoded
				if ((RF_DATA_STATUS & RF_DATA_RECEIVED_MASK) != 0)
				{
					uart_command = NONE;
					uart_put_RF_CODE_Data(RF_CODE_LEARN_OK);

					// clear RF status
					RF_DATA_STATUS = 0;
				}
				// check for learning timeout
				if (IsTimerFinished())
				{
					InitTimer_ms(1, 1000);
					BUZZER = BUZZER_ON;
					// wait until timer has finished
					WaitTimerFinsihed();
					BUZZER = BUZZER_OFF;
					// send not-acknowledge
					uart_put_command(RF_CODE_LEARN_KO);
					uart_command = NONE;
				}
				break;

			// do original sniffing
			case RF_CODE_RFIN:
				// check if a RF signal got decoded
				if ((RF_DATA_STATUS & RF_DATA_RECEIVED_MASK) != 0)
				{
					uart_put_RF_CODE_Data(RF_CODE_RFIN);

					// clear RF status
					RF_DATA_STATUS = 0;
				}
				break;

			// do original transfer
			case RF_CODE_RFOUT:
				// only do the job if all data got received by UART
				if (uart_state != IDLE)
					break;

				if (sniffing_is_on)
					PCA0_StopSniffing();

				// byte 0..1:	Tsyn
				// byte 2..3:	Tlow
				// byte 4..5:	Thigh
				// byte 6..7:	24bit Data
				// set low time of sync to 2000�s - unknown
				// set duty cycle of high and low bit to 70 and 30 % - unknown
				PCA0_InitTransmit(*(uint16_t *)&RF_DATA[0], 2000,
						*(uint16_t *)&RF_DATA[4], 70, *(uint16_t *)&RF_DATA[2], 30, 24);

				actual_byte = 7;

				// start RF transmit
				StartRFTransmit();

				uart_command = NONE;
				break;

			// do new sniffing
			case RF_CODE_SNIFFING_ON:
				// check if a RF signal got decoded
				if ((RF_DATA_STATUS & RF_DATA_RECEIVED_MASK) != 0)
				{
					uint8_t used_protocol = RF_DATA_STATUS & 0x7F;
					uart_put_RF_Data(RF_CODE_SNIFFING_ON, used_protocol);

					// clear RF status
					RF_DATA_STATUS = 0;
				}
				break;

			// transmit data on RF
			case RF_CODE_TRANSMIT_DATA_NEW:
				// only do the job if all data got received by UART
				if (uart_state != IDLE)
					break;

				if (sniffing_is_on)
					PCA0_StopSniffing();

				// check if unknown protocol should be used
				// byte 0:		0x7F Protocol identifier
				// byte 1..2:	SYNC_HIGH
				// byte 3..4:	SYNC_LOW
				// byte 5..6:	BIT_HIGH_TIME
				// byte 7:		BIT_HIGH_DUTY
				// byte 8..9:	BIT_LOW_TIME
				// byte 10:		BIT_LOW_DUTY
				// byte 11:		BIT_COUNT + SYNC_BIT_COUNT in front of RF data
				// byte 12..N:	RF data to send
				if (RF_DATA[0] == 0x7F)
				{
					PCA0_InitTransmit(*(uint16_t *)&RF_DATA[1], *(uint16_t *)&RF_DATA[3],
							*(uint16_t *)&RF_DATA[5], RF_DATA[7], *(uint16_t *)&RF_DATA[8], RF_DATA[10], RF_DATA[11]);

					actual_byte = 12;
				}
				// byte 0:		Protocol identifier 0x01..0x7E
				// byte 1..N:	data to be transmitted
				else
				{
					protocol_index = PCA0_GetProtocolIndex(RF_DATA[0]);

					if (protocol_index != 0xFF)
					{
						PCA0_InitTransmit(PROTOCOL_DATA[protocol_index].SYNC_HIGH, PROTOCOL_DATA[protocol_index].SYNC_LOW,
								PROTOCOL_DATA[protocol_index].BIT_HIGH_TIME, PROTOCOL_DATA[protocol_index].BIT_HIGH_DUTY,
								PROTOCOL_DATA[protocol_index].BIT_LOW_TIME, PROTOCOL_DATA[protocol_index].BIT_LOW_DUTY,
								PROTOCOL_DATA[protocol_index].BIT_COUNT);

						actual_byte = 1;
					}
					else
					{
						uart_command = NONE;
					}
				}

				// if valid RF protocol start RF transmit
				if (uart_command != NONE)
					StartRFTransmit();

				uart_command = NONE;
				break;
		}
	}
}
