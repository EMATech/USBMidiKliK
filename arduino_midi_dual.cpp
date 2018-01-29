/***********************************************************************
 *  ARDUINO_MIDI_DUAL FIRMWARE -
 *  Franck Touanen - 2017.01.16
 *
 *  Based on :
 *     . The wonderful LUFA low-level midi lib and examples by Dean Camera
 *               (http://www.fourwalledcubicle.com/LUFA.php)
 *     . Inspired by HIDUINO by Dimitri Diakopoulos
 *               (http://www.dimitridiakopoulos.com)
 *     . Inspired also by dualMocoLUFA Project by morecat_la
 *               (http://morecatlab.akiba.coocan.jp/)
 *	   . Francois Best's Arduino MIDI Library, for sure the best one !
 *						(https://github.com/FortySevenEffects/arduino_midi_library)
 *
 *  Compiled against the last LUFA version / MIDI library
 ***********************************************************************/

#include "arduino_midi_dual.h"

uint16_t tx_ticks = 0;
uint16_t rx_ticks = 0;
const uint16_t TICK_COUNT = 5000;

bool MIDIBootMode  = false;
bool MIDIHighSpeed = false;	// 0: normal speed(31250bps), 1: high speed (1250000bps)

// Ring Buffers

static RingBuffer_t USBtoUSART_Buffer;           // Circular buffer to hold host data
static uint8_t      USBtoUSART_Buffer_Data[128]; // USB to USART_Buffer
static RingBuffer_t USARTtoUSB_Buffer;           // Circular buffer to hold data from the serial port
static uint8_t      USARTtoUSB_Buffer_Data[128]; // USART to USB_Buffer

extern USB_ClassInfo_MIDI_Device_t Keyboard_MIDI_Interface;
extern USB_ClassInfo_CDC_Device_t VirtualSerial_CDC_Interface;

///////////////////////////////////////////////////////////////////////////////
// MAIN START HERE
///////////////////////////////////////////////////////////////////////////////

int  main(void)
{

	SetupHardware();
	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);

	if (MIDIBootMode) ProcessMidiUsbMode(); // Inifinite loop

	else ProcessSerialUsbMode();

///////////////////////////////////////////////////////////////////////////////
}

///////////////////////////////////////////////////////////////////////////////
// SETUP HARWARE WITH DUAL BOOT SERIAL / MIDI
//
// WHEN PB2 IS GROUNDED during reset, this allows to reflash the MIDI application
// firmware with the standard Arduino IDE without reflashing the usbserial bootloader.
//
// Original inspiration from dualMocoLUFA Project by morecat_lab
//     http://morecatlab.akiba.coocan.jp/
//
// Note : When PB3 is grounded, this allows the HighSpeed mode for MIDI
///////////////////////////////////////////////////////////////////////////////

void SetupHardware(void)
{
#if (ARCH == ARCH_AVR8)
	// Disable watchdog if enabled by bootloader/fuses
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	// Disable clock division
	clock_prescale_set(clock_div_1);

#endif

	// Pins assignments :
	// PB1 = LED
	// PB2 = (MIDI/ Arduino SERIAL)
	// PB3 = (NORMAL/HIGH) SPEED

  DDRB  = 0x02;		// SET PB1 as OUTPUT and PB2/PB3 as INPUT
  PORTB = 0x0C;		// PULL-UP PB2/PB3

	MIDIBootMode  = ( (PINB & 0x04) == 0 ) ? false : true;
	MIDIHighSpeed = ( (PINB & 0x08) == 0 ) ? true  : false ;

  if (MIDIBootMode) {
		if ( MIDIHighSpeed ) Serial_Init(1250000, false);
		else 		Serial_Init(31250, false);
	}
	else Serial_Init(9600, false);

	/* Hardware Initialization */
	LEDs_Init();
	USB_Init();

	// Start the flush timer so that overflows occur rapidly to
	// push received bytes to the USB interface
	TCCR0B = (1 << CS02);

	/* Pull target /RESET line high */
	AVR_RESET_LINE_PORT |= AVR_RESET_LINE_MASK;
	AVR_RESET_LINE_DDR  |= AVR_RESET_LINE_MASK;


}


///////////////////////////////////////////////////////////////////////////////
// USB Configuration and  events
///////////////////////////////////////////////////////////////////////////////

// Event handler for the library USB Connection event
void EVENT_USB_Device_Connect(void) {
	LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);
}

// Event handler for the library USB Disconnection event
void EVENT_USB_Device_Disconnect(void) {
	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

// Event handler for the USB_ConfigurationChanged event. This is fired when the host set the current configuration
// of the USB device after enumeration - the device endpoints are configured and the MIDI management task started.
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	if (MIDIBootMode) {
		// Setup MIDI Data Endpoints
		ConfigSuccess &= Endpoint_ConfigureEndpoint(MIDI_STREAM_IN_EPADDR, EP_TYPE_BULK, MIDI_STREAM_EPSIZE, 1);
		ConfigSuccess &= Endpoint_ConfigureEndpoint(MIDI_STREAM_OUT_EPADDR, EP_TYPE_BULK, MIDI_STREAM_EPSIZE, 1);
		//ConfigSuccess &= MIDI_Device_ConfigureEndpoints(&Keyboard_MIDI_Interface);
	}
	else {
    ConfigSuccess &= CDC_Device_ConfigureEndpoints(&VirtualSerial_CDC_Interface);
	}

	LEDs_SetAllLEDs(ConfigSuccess ? LEDMASK_USB_READY : LEDMASK_USB_ERROR);
}

// Event handler for the library USB Control Request event.
void EVENT_USB_Device_ControlRequest(void)
{
	if (MIDIBootMode)
	    MIDI_Device_ProcessControlRequest(&Keyboard_MIDI_Interface);
	else
	   CDC_Device_ProcessControlRequest(&VirtualSerial_CDC_Interface);
}

// Event handler for the CDC Class driver Host-to-Device Line Encoding Changed event.
void EVENT_CDC_Device_ControLineStateChanged(USB_ClassInfo_CDC_Device_t* const CDCInterfaceInfo)
{
	bool CurrentDTRState = (CDCInterfaceInfo->State.ControlLineStates.HostToDevice & CDC_CONTROL_LINE_OUT_DTR);

	if (CurrentDTRState)
	  AVR_RESET_LINE_PORT &= ~AVR_RESET_LINE_MASK;
	else
	  AVR_RESET_LINE_PORT |= AVR_RESET_LINE_MASK;
}

// Event handler for the CDC Class driver Line Encoding Changed event.
void EVENT_CDC_Device_LineEncodingChanged(USB_ClassInfo_CDC_Device_t* const CDCInterfaceInfo)
{
	uint8_t ConfigMask = 0;

	switch (CDCInterfaceInfo->State.LineEncoding.ParityType)
	{
		case CDC_PARITY_Odd:
			ConfigMask = ((1 << UPM11) | (1 << UPM10));
			break;
		case CDC_PARITY_Even:
			ConfigMask = (1 << UPM11);
			break;
	}

	if (CDCInterfaceInfo->State.LineEncoding.CharFormat == CDC_LINEENCODING_TwoStopBits)
		ConfigMask |= (1 << USBS1);

	switch (CDCInterfaceInfo->State.LineEncoding.DataBits)
	{
		case 6:
			ConfigMask |= (1 << UCSZ10);
			break;
		case 7:
			ConfigMask |= (1 << UCSZ11);
			break;
		case 8:
			ConfigMask |= ((1 << UCSZ11) | (1 << UCSZ10));
			break;
	}

	// Must turn off USART before reconfiguring it, otherwise incorrect operation may occur
	UCSR1B = 0;
	UCSR1A = 0;
	UCSR1C = 0;

	// Special case 57600 baud for compatibility with the ATmega328 bootloader.
	UBRR1  = (CDCInterfaceInfo->State.LineEncoding.BaudRateBPS == 57600)
			 ? SERIAL_UBBRVAL(CDCInterfaceInfo->State.LineEncoding.BaudRateBPS)
			 : SERIAL_2X_UBBRVAL(CDCInterfaceInfo->State.LineEncoding.BaudRateBPS);

	UCSR1C = ConfigMask;
	UCSR1A = (CDCInterfaceInfo->State.LineEncoding.BaudRateBPS == 57600) ? 0 : (1 << U2X1);
	UCSR1B = ((1 << RXCIE1) | (1 << TXEN1) | (1 << RXEN1));
}

///////////////////////////////////////////////////////////////////////////////
// Serial Worker Functions
// Used when Arduino is in USB Serial mode.
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// INFINITE LOOP FOR USB SERIAL PROCESSING
// ----------------------------------------------------------------------------
static void ProcessSerialUsbMode(void) {

	RingBuffer_InitBuffer(&USBtoUSART_Buffer, USBtoUSART_Buffer_Data, sizeof(USBtoUSART_Buffer_Data));
	RingBuffer_InitBuffer(&USARTtoUSB_Buffer, USARTtoUSB_Buffer_Data, sizeof(USARTtoUSB_Buffer_Data));
	GlobalInterruptEnable();

	for (;;)
		{
		 // Only try to read bytes from the CDC interface if the transmit buffer is not full

		 if (!(RingBuffer_IsFull(&USBtoUSART_Buffer))) {

			 int16_t ReceivedByte = CDC_Device_ReceiveByte(&VirtualSerial_CDC_Interface);
			 // Store received byte into the USART transmit buffer
			 if (!(ReceivedByte < 0))
				 RingBuffer_Insert(&USBtoUSART_Buffer, ReceivedByte);
		 }

		 uint16_t BufferCount = RingBuffer_GetCount(&USARTtoUSB_Buffer);
		 if (BufferCount) {
			 Endpoint_SelectEndpoint(VirtualSerial_CDC_Interface.Config.DataINEndpoint.Address);

			 // Check if a packet is already enqueued to the host - if so, we shouldn't try to send more data
			// until it completes as there is a chance nothing is listening and a lengthy timeout could occur
			 if (Endpoint_IsINReady()) {
				 // Never send more than one bank size less one byte to the host at a time, so that we don't block
			// while a Zero Length Packet (ZLP) to terminate the transfer is sent if the host isn't listening
				 uint8_t BytesToSend = MIN(BufferCount, (CDC_TXRX_EPSIZE - 1));

				 // Read bytes from the USART receive buffer into the USB IN endpoint
				 while (BytesToSend--) {
					 // Try to send the next byte of data to the host, abort if there is an error without dequeuing
					 if (CDC_Device_SendByte(&VirtualSerial_CDC_Interface,
											 RingBuffer_Peek(&USARTtoUSB_Buffer)) != ENDPOINT_READYWAIT_NoError) break;
					 // Dequeue the already sent byte from the buffer now we have confirmed that no transmission error occurred
					 RingBuffer_Remove(&USARTtoUSB_Buffer);
				 }
			 }
		 }

		 // Load the next byte from the USART transmit buffer into the USART if transmit buffer space is available
		 if (Serial_IsSendReady() && !(RingBuffer_IsEmpty(&USBtoUSART_Buffer))  )
			 Serial_SendByte( RingBuffer_Remove(&USBtoUSART_Buffer) );

		 CDC_Device_USBTask(&VirtualSerial_CDC_Interface);
		 USB_USBTask();
	 }
}

///////////////////////////////////////////////////////////////////////////////
// MIDI Worker Functions
// Used when Arduino is in USB MIDI mode (default mode).
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// INFINITE LOOP FOR USB MIDI AND SERIAL MIDI PROCESSING
// ----------------------------------------------------------------------------
static void ProcessMidiUsbMode(void) {

	// This ringbuffer is populated by the ISR below
	RingBuffer_InitBuffer(&USARTtoUSB_Buffer, USARTtoUSB_Buffer_Data, sizeof(USARTtoUSB_Buffer_Data));

	UCSR1B |= (1 << RXCIE1 ); // Enable the USART Receive Complete interrupt ( USART_RXC )
	//sei () ; // Enable the Global Interrupt Enable flag so that interrupts can be processe
	GlobalInterruptEnable();

	for (;;) {

		if (tx_ticks > 0) tx_ticks--;
		else if (tx_ticks == 0) LEDs_TurnOffLEDs(LEDS_LED2);

		if (rx_ticks > 0) rx_ticks--;
		else if (rx_ticks == 0) LEDs_TurnOffLEDs(LEDS_LED1);

		// Device must be connected and configured for the task to run
		if (USB_DeviceState == DEVICE_STATE_Configured) {

			if (!(RingBuffer_IsEmpty(&USARTtoUSB_Buffer) ) ) {
				if (ProcessMidiToUsb(RingBuffer_Remove(&USARTtoUSB_Buffer))) {
					LEDs_TurnOnLEDs(LEDS_LED1);
					rx_ticks = TICK_COUNT;
				}
			}

			ProcessUsbToMidi();

		}

    MIDI_Device_USBTask(&Keyboard_MIDI_Interface);
		USB_USBTask();
	}
}

// ----------------------------------------------------------------------------
// MIDI USB  - Table 4-1: Code IndexNumber Classifications of MIDI10.PDF
// ----------------------------------------------------------------------------
/*
 The first byte in each 32-bit USB-MIDI Event Packet is a Packet Header
 contains a Cable Number (4 bits) followed by a Code Index Number (4 bits).
 The remaining threebytes contain the actual MIDI event.
 Most typical parsed MIDI events are two or threebytes in length.

 The Cable Number (C N) is a value ranging from 0x0 to 0xF indicating the number
 assignment of the Embedded MIDI Jack associated with the endpoint that is
 transferring the data.
 The Code Index Number (CIN) indicates the classification of the bytes in
 the MIDI_x fields. The following table summarizes these classifications.

(Table 4-1: Code IndexNumber Classifications of MIDI10.PDF)                */
static const int BytesIn_USB_MIDI_Command[] =
{
	 0,	/* 0 - function codes (reserved) */
	 0,	/* 1 - cable events (reserved) */
	 2,	/* 2 - two-byte system common message */
	 3,	/* 3 - three-byte system common message */
	 3,	/* 4 - sysex starts or continues */
	 1,	/* 5 - sysex ends with one byte, or single-byte system common message */
	 2,	/* 6 - sysex ends with two bytes */
	 3,	/* 7 - sysex ends with three bytes */
	 3,	/* 8 - note-off */
	 3,	/* 9 - note-on */
	 3,	/* A - poly-keypress */
	 3,	/* B - control change */
	 2,	/* C - program change */
	 2,	/* D - channel pressure */
	 3,	/* E - pitch bend change */
	 1,	/* F - single byte */
 };

// ----------------------------------------------------------------------------
// MIDI PARSER
// Check whether we've received any MIDI data from the USART, and if it's
// complete send it over USB. return true if a packet was sent
// ----------------------------------------------------------------------------
static bool ProcessMidiToUsb(uint8_t receivedByte)
{
	static  MIDI_EventPacket_t MIDIEvent;
	static  bool sysExMode = false;
  static  uint8_t midiEventBufferIndex = 0;
	static	uint8_t nextMidiMsgLength=0;

	// Real-time message -- send immediately
	if ( receivedByte >= 0xF8) {
			 MIDIEvent.Event 		= 0xF;
		   MIDIEvent.Data1    = receivedByte;
			 MIDI_SendEventPacket(&MIDIEvent,1);
			 midiEventBufferIndex = 0;
			 return true;
	}
  // SYSTEM EXCLUSIVE

	else if (receivedByte == 0xF0) sysExMode = true;  // Start SYSEX
	else if (receivedByte == 0xF7) sysExMode = false; // SYSEX END

	else if (receivedByte >= 0x80)  {
		/* A new command */
		midiEventBufferIndex = 0;
		nextMidiMsgLength = BytesIn_USB_MIDI_Command[receivedByte >> 4];
	}
	else if (midiEventBufferIndex == 0 && !sysExMode)
	{
		/* Expecting a command but this isn't one -- ignore it */
		return false;
	}

	memset( (void*) (&MIDIEvent + midiEventBufferIndex),receivedByte, 1);
	midiEventBufferIndex++;

	// Process SYSEX
	if (sysExMode) {
		if (midiEventBufferIndex == 3)
		{
			MIDIEvent.Event 		= 0x4;
			MIDI_SendEventPacket(&MIDIEvent,midiEventBufferIndex);
			midiEventBufferIndex = 0;
			return true;
		}
	}
	else if (receivedByte == 0xF7)
	{
		/* End of SysEx -- send the last bytes */
		MIDIEvent.Event 		= 0x4 + midiEventBufferIndex;
		MIDI_SendEventPacket(&MIDIEvent,midiEventBufferIndex);
		midiEventBufferIndex = 0;
		return true;
	}
	else if (midiEventBufferIndex == nextMidiMsgLength)
	{
		MIDIEvent.Event 		= MIDIEvent.Event >> 4;
		MIDI_SendEventPacket(&MIDIEvent,midiEventBufferIndex);
		midiEventBufferIndex = 1;
		return true;
	}
	return false;
}
// ----------------------------------------------------------------------------
// Send a MIDI Event packet to the USB
// ----------------------------------------------------------------------------

static void MIDI_SendEventPacket(const MIDI_EventPacket_t *MIDIEvent,uint8_t dataSize)
{
	// Zero padding
	if (dataSize < 3 ) {
				memset(  (void*) (MIDIEvent + dataSize  + 1),0, sizeof(MIDI_EventPacket_t)- dataSize - 1);
	}
	MIDI_Device_SendEventPacket(&Keyboard_MIDI_Interface, MIDIEvent);
	MIDI_Device_Flush(&Keyboard_MIDI_Interface);
}

// ----------------------------------------------------------------------------
// Read a MIDI USB event and send it to the USART.
// ----------------------------------------------------------------------------

static void ProcessUsbToMidi(void)
{
	MIDI_EventPacket_t ReceivedMIDIEvent;
	while (MIDI_Device_ReceiveEventPacket(&Keyboard_MIDI_Interface, &ReceivedMIDIEvent))
	{
			// Passthrough to Arduino
			SerialMIDI_SendEventPacket(&ReceivedMIDIEvent);
	}

	LEDs_TurnOnLEDs(LEDS_LED1);
	tx_ticks = TICK_COUNT;

	/* If the endpoint is now empty, clear the bank */
	if ( !(Endpoint_BytesInEndpoint()) ) Endpoint_ClearOUT();

}

// ----------------------------------------------------------------------------
// Send a MIDI USB event to the USART.
// ----------------------------------------------------------------------------

static void SerialMIDI_SendEventPacket(const MIDI_EventPacket_t *Event)
{
	static int16_t lastStatusSent=-1;
	uint8_t   BytesIn;
	bool 			NoRunningStatus ;
	uint8_t		command = Event->Event & 0x0F;

	BytesIn = BytesIn_USB_MIDI_Command[command];

	/* Don't do the running status optimisation on internal messages, SysEx
	 * messages, or single bytes. */
	switch (command) {
			case 0x0: case 0x1: case 0x4: case 0x5: case 0x6:
			case 0x7: case 0xF:
				NoRunningStatus = true;
				break;
	default:
			  NoRunningStatus = false;
				break;
	}

	if (BytesIn >= 1)
	{
		if (NoRunningStatus || Event->Data1 != lastStatusSent)
		{
			Serial_SendByte(Event->Data1);
			lastStatusSent = Event->Data1;
		}
	}
	if (BytesIn >= 2) Serial_SendByte(Event->Data2);
	if (BytesIn >= 3) Serial_SendByte(Event->Data3);
}

///////////////////////////////////////////////////////////////////////////////
// ISR to manage the reception of data from the midi/serial port, placing
// received bytes into a circular buffer for later transmission to the host.
///////////////////////////////////////////////////////////////////////////////
// Parse via Arduino/Serial
ISR(USART1_RX_vect, ISR_BLOCK)
{
	RingBuffer_Insert(&USARTtoUSB_Buffer, UDR1);
}