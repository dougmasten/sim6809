/* vim: set noexpandtab ai ts=4 sw=4 tw=4:
   acia.c -- emulation of 6850 ACIA
   Copyright (C) 2012 Gordon JC Pearce

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <termios.h>

#include "../emu/config.h"
#include "../emu/emu6809.h"
#include "../emu/console.h"
#include "acia.h"
#include "hardware.h"

/*
   The 6850 ACIA in the Mirage is mapped at $E100
   registers are:
   $e100 control register / status register
   $e101 transmit register / receive register
   I only intend to implement CR7 RX interrupt and CR5 TX interrupt in the control
   and SR0 RD full, SR1 TX empty, and SR7 IRQ
   maybe for robustness testing I will implement a way to signal errors
   RTS, CTS and DCD are not used, with the latter two held grounded
*/

#define BUFZ 256

static uint8_t ibuf[BUFZ];
static int ilen = 0;
static int ipos;

static uint8_t obuf[BUFZ];
static int olen = 0;
static int omode = 0;
#define O_BUFMASK 3
#define O_LBUF 0
#define O_ZBUF 1
#define O_NBUF 2
#define O_FLUSH 4
#define O_ECHO  8
#define O_IE 128

static int irqen = 0;
static int irqf  = 0;

static int fc;

static void acia_stop(void) {
    fcntl(0, F_SETFL, fc & ~O_NONBLOCK);
}

static void acia_start(void) {
    fcntl(0, F_SETFL, fc | O_NONBLOCK);
}

int acia_init(int argc, char *argv[]) {
    ilen = 0;
    ipos = 0;
    olen = 0;
    omode = 0;
    irqen = 0;
    irqf = 0;
    fc = fcntl(0, F_GETFL, 0);
    acia_stop();
    hard_addfd(0);
    return 0;
}

void acia_deinit() {
}

void acia_run() {
    int ret;

    acia_start();
    if (ilen == 0) {
	ret = read(0, ibuf, BUFZ);
	if (ret > 0) {
	    ilen = ret;
	    ipos = 0;
	    if (irqen) {
		irqf = 1;
		irq();
	    }
	}
    }
    acia_stop();
}


uint8_t acia_rreg(int reg) {
    uint8_t i = 0;
    switch (reg & 0x01) {
    case 0:
	if (ilen) i |= 1;
	if (irqf) i |= 0x80;
	irqf = 0;
	return i;
    case 1:
	if (ilen) {
	    ilen--;
	    return ibuf[ipos++];
	}
    }
    return 0x00;
}

void acia_wreg(int reg, uint8_t val) {
    struct termios t;
    tcgetattr(0, &t);

    switch (reg & 0x01) {
    case 0:

	if (val & O_IE) irqen = 1;

	omode = val & O_BUFMASK;
	switch (omode) {
	case O_LBUF:
	case O_ZBUF:
	    t.c_lflag |= ICANON; break;
	case O_NBUF:
	    t.c_lflag &= ~ICANON; break;
	}

	if (val & O_ECHO)
	    t.c_lflag |= ECHO;
	else
	    t.c_lflag &= ~ECHO;
	tcsetattr(1, TCSANOW, &t);

	if (val & O_FLUSH) {
	    write(1,obuf,olen);

	}
	break;
    case 1:
	obuf[olen++] = val;
	if (omode == O_NBUF ||
	    (omode == O_LBUF && val == '\n') ||
	     olen == BUFZ) {
	    write(1,obuf,olen);
	    olen = 0;
	}
    }
}
