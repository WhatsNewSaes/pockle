/*****************************************************************************
* | File      	:   EPD_3IN97.c
* | Author      :   Waveshare team
* | Function    :   3.97inch e-paper
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2019-06-11
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
// Pixel League: register sequences are the vendor's, unchanged. Transport is
// hardware SPI with whole-frame bulk writes, the per-row sleeps and the fixed
// 100 ms busy pre-delay are gone, and non-blocking update entry points let the
// application keep handling input while the panel refreshes.
#include "EPD_3in97.h"

static const UDOUBLE FRAME_BYTES = (UDOUBLE)(EPD_3IN97_WIDTH / 8) * EPD_3IN97_HEIGHT;

// BUSY asserts within microseconds of a command; a short guard covers that
// window so a poll never reports idle before the update has begun.
static uint32_t busyGuardUntil = 0;
static void EPD_3IN97_ArmBusy(void) { busyGuardUntil = millis() + 5; }

bool EPD_3IN97_Busy(void)
{
    if ((int32_t)(millis() - busyGuardUntil) < 0) return true;
    return DEV_Digital_Read(EPD_BUSY_PIN) == 1;      //LOW: idle, HIGH: busy
}

/******************************************************************************
function :	Software reset
parameter:
******************************************************************************/
static void EPD_3IN97_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
    EPD_3IN97_ArmBusy();
}

/******************************************************************************
function :	send command
parameter:
     Reg : Command register
******************************************************************************/
static void EPD_3IN97_SendCommand(UBYTE Reg)
{
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	send data
parameter:
    Data : Write data
******************************************************************************/
static void EPD_3IN97_SendData(UBYTE Data)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

// Stream a whole RAM window with CS held low.
static void EPD_3IN97_SendBuffer(const UBYTE *Data, UDOUBLE Length)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_Write_nByte(Data, Length);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
static void EPD_3IN97_ReadBusy(void)
{
    uint32_t started = millis();
    while(EPD_3IN97_Busy()) {
        if (millis() - started > 15000) { Serial.println("DISPLAY BUSY TIMEOUT"); return; }
        DEV_Delay_ms(1);
    }
}

void EPD_3IN97_WaitIdle(void) { EPD_3IN97_ReadBusy(); }

/******************************************************************************
function :	Turn On Display
parameter:  sequence : display update control 2 value (vendor: F7 full, D7 fast, FF partial)
******************************************************************************/
static void EPD_3IN97_StartDisplay(UBYTE sequence)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(sequence);
    EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ArmBusy();
}

static void EPD_3IN97_TurnOnDisplay(void)      { EPD_3IN97_StartDisplay(0xF7); EPD_3IN97_ReadBusy(); }
static void EPD_3IN97_TurnOnDisplay_Fast(void) { EPD_3IN97_StartDisplay(0xD7); EPD_3IN97_ReadBusy(); }
static void EPD_3IN97_TurnOnDisplay_Part(void) { EPD_3IN97_StartDisplay(0xFF); EPD_3IN97_ReadBusy(); }

/******************************************************************************
function :	Initialize the e-Paper register
parameter:
******************************************************************************/
void EPD_3IN97_Init(void)
{
    EPD_3IN97_Reset();

    EPD_3IN97_ReadBusy();
    EPD_3IN97_SendCommand(0x12);  //SWRESET
    EPD_3IN97_ArmBusy();
    EPD_3IN97_ReadBusy();

    EPD_3IN97_SendCommand(0x18);
    EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x0C);
	EPD_3IN97_SendData(0xAE);
	EPD_3IN97_SendData(0xC7);
	EPD_3IN97_SendData(0xC3);
	EPD_3IN97_SendData(0xC0);
	EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x01); //Driver output control
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
    EPD_3IN97_SendData(0x02);

    EPD_3IN97_SendCommand(0x3C); //BorderWavefrom
    EPD_3IN97_SendData(0x01);

    EPD_3IN97_SendCommand(0x11); //data entry mode
	EPD_3IN97_SendData(0x01);

	EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)%256);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)/256);

	EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);

    EPD_3IN97_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendCommand(0x4F);   // set RAM y address count to 0X199;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
    EPD_3IN97_ReadBusy();

}
//Fast update initialization
void EPD_3IN97_Init_Fast(void)
{
	EPD_3IN97_Reset();

	EPD_3IN97_ReadBusy();
	EPD_3IN97_SendCommand(0x12);  //SWRESET
	EPD_3IN97_ArmBusy();
	EPD_3IN97_ReadBusy();

	EPD_3IN97_SendCommand(0x0C);
	EPD_3IN97_SendData(0xAE);
	EPD_3IN97_SendData(0xC7);
	EPD_3IN97_SendData(0xC3);
	EPD_3IN97_SendData(0xC0);
	EPD_3IN97_SendData(0x80);

	EPD_3IN97_SendCommand(0x01); //Driver output control
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
	EPD_3IN97_SendData(0x02);

	EPD_3IN97_SendCommand(0x11); //data entry mode
	EPD_3IN97_SendData(0x01);

	EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)%256);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)/256);

	EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);


	EPD_3IN97_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendCommand(0x4F);   // set RAM y address count to 0X199;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
    EPD_3IN97_ReadBusy();

	EPD_3IN97_SendCommand(0x3C); //BorderWavefrom
	EPD_3IN97_SendData(0x01);

	EPD_3IN97_SendCommand(0x18);
	EPD_3IN97_SendData(0x80);
	//Fast(1.5s)
	EPD_3IN97_SendCommand(0x1A);
	EPD_3IN97_SendData(0x6A);

}

static void EPD_3IN97_SetCursor(void)
{
    EPD_3IN97_SendCommand(0x4E);   // RAM x address counter
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendCommand(0x4F);   // RAM y address counter
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendData(0x00);
}

/******************************************************************************
function :	Clear screen
parameter:
******************************************************************************/
static void EPD_3IN97_Fill(UBYTE value)
{
    static UBYTE line[EPD_3IN97_WIDTH / 8];
    memset(line, value, sizeof(line));
    EPD_3IN97_SendCommand(0x24);
    for (UWORD j = 0; j < EPD_3IN97_HEIGHT; j++) EPD_3IN97_SendBuffer(line, sizeof(line));
    EPD_3IN97_SendCommand(0x26);
    for (UWORD j = 0; j < EPD_3IN97_HEIGHT; j++) EPD_3IN97_SendBuffer(line, sizeof(line));
    EPD_3IN97_TurnOnDisplay();
}

void EPD_3IN97_Clear(void)       { EPD_3IN97_Fill(0xFF); }
void EPD_3IN97_Clear_Black(void) { EPD_3IN97_Fill(0x00); }

/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
static void EPD_3IN97_WriteFrame(const UBYTE *Image, bool both)
{
    EPD_3IN97_SendCommand(0x3C);   // normal border for full/fast refreshes
    EPD_3IN97_SendData(0x01);
    EPD_3IN97_SetCursor();
    EPD_3IN97_SendCommand(0x24);
    EPD_3IN97_SendBuffer(Image, FRAME_BYTES);
    if (both) {
        EPD_3IN97_SetCursor();
        EPD_3IN97_SendCommand(0x26);
        EPD_3IN97_SendBuffer(Image, FRAME_BYTES);
    }
}

void EPD_3IN97_Display(const UBYTE *Image)           { EPD_3IN97_WriteFrame(Image, false); EPD_3IN97_TurnOnDisplay(); }
void EPD_3IN97_Display_Base(const UBYTE *Image)      { EPD_3IN97_WriteFrame(Image, true);  EPD_3IN97_TurnOnDisplay(); }
void EPD_3IN97_Display_Fast(const UBYTE *Image)      { EPD_3IN97_WriteFrame(Image, false); EPD_3IN97_TurnOnDisplay_Fast(); }
void EPD_3IN97_Display_Fast_Base(const UBYTE *Image) { EPD_3IN97_WriteFrame(Image, true);  EPD_3IN97_TurnOnDisplay_Fast(); }

void EPD_3IN97_Display_Base_Async(const UBYTE *Image)      { EPD_3IN97_WriteFrame(Image, true); EPD_3IN97_StartDisplay(0xF7); }
void EPD_3IN97_Display_Fast_Base_Async(const UBYTE *Image) { EPD_3IN97_WriteFrame(Image, true); EPD_3IN97_StartDisplay(0xD7); }

/******************************************************************************
function :	Partial (mode 2) refresh of the whole frame
parameter:  Image    : new frame
            Previous : frame currently on the panel (may be NULL after a base write)
Pixel League: the vendor sequence hardware-resets the controller before every
partial write and then uses the controller's default addressing, which does not
match how the base image was written, and it never refreshes the "old" RAM. On
this panel that left partial updates invisible until the next full refresh.
Keep the initialised state instead, load both RAM planes with the same
addressing as the base image, and run the partial waveform.
******************************************************************************/
static void EPD_3IN97_WritePartial(const UBYTE *Image, const UBYTE *Previous)
{
    EPD_3IN97_SendCommand(0x3C);   // border floating during partial updates
    EPD_3IN97_SendData(0x80);
    if (Previous) {
        EPD_3IN97_SetCursor();
        EPD_3IN97_SendCommand(0x26);
        EPD_3IN97_SendBuffer(Previous, FRAME_BYTES);
    }
    EPD_3IN97_SetCursor();
    EPD_3IN97_SendCommand(0x24);
    EPD_3IN97_SendBuffer(Image, FRAME_BYTES);
}

void EPD_3IN97_Display_Partial(const UBYTE *Image, const UBYTE *Previous)
{
	EPD_3IN97_WritePartial(Image, Previous);
	EPD_3IN97_TurnOnDisplay_Part();
}

void EPD_3IN97_Display_Partial_Async(const UBYTE *Image, const UBYTE *Previous)
{
	EPD_3IN97_WritePartial(Image, Previous);
	EPD_3IN97_StartDisplay(0xFF);
}

/******************************************************************************
function :	Enter sleep mode
parameter:
******************************************************************************/
void EPD_3IN97_Sleep(void)
{
    EPD_3IN97_SendCommand(0x10); //enter deep sleep
    EPD_3IN97_SendData(0x01);
    DEV_Delay_ms(100);
}
