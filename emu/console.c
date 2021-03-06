/* console.c -- debug console
   Copyright (C) 1998 Jerome Thoen

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
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>

#include "config.h"
#include "emu6809.h"
#include "motorola.h"
#include "console.h"

#include "../hardware/hardware.h"
#include "../hardware/reset.h"

long cycles = 0;

int activate_console = 0;
int watchpoint = -1;
static int console_active = 0;
static int autof = 1;
static int cps = 50000;
static volatile int alrmf = 0;
static int noquit = 0;
static struct termios tc_orig;
static struct termios tc_cmd;
static struct termios tc_exec;


static void sigbrkhandler(int sigtype)
{
  if (!console_active)
    activate_console = 1;
}

static void setup_brkhandler(void)
{
  signal(SIGINT, sigbrkhandler);
}

void console_init(void)
{
  printf("sim6809 v0.1 - 6809 simulator\nCopyright (c) 1998 by Jerome Thoen\n\n");
  if (tcgetattr(0,&tc_orig) < 0) {
      perror("in console_init(): tcgetattr");
  }
  tcgetattr(0, &tc_cmd);
  tcgetattr(0, &tc_exec);
}


static void do_alarm(int sig) {
    alrmf = 1;
    signal(SIGALRM, do_alarm);
    alarm(1);
}


int execute()
{
  int n;
  int r = 0;
  int i = 0;

  tcsetattr(0, TCSANOW, &tc_exec);
  do {
    while ((n = m6809_execute()) > 0 && !activate_console) {
      cycles += n;
      i += n;
      if (cps && i > cps) {
	  pause();
	  if (alrmf) i = 0;
      }
      hard_poll();
    }
    if (activate_console && n > 0)
      cycles += n;

    if (n < 0) {
      printf("m6809 run time error, return code %d\n", n);
      activate_console = r = 1;
    }
  } while (!activate_console);
  tcgetattr(0, &tc_exec);
  tcsetattr(0, TCSANOW, &tc_cmd);
  return r;
}

void execute_addr(tt_u16 addr)
{
  int n;
  tcsetattr(0, TCSANOW, &tc_exec);
  while (!activate_console && rpc != addr) {
    while ((n = m6809_execute()) > 0 && !activate_console && rpc != addr) {
      cycles += n;
      hard_poll();
    }
    if (n < 0) {
      printf("m6809 run time error, return code %d\n", n);
      activate_console = 1;
    }
  }
  tcgetattr(0, &tc_exec);
  tcsetattr(0, TCSANOW, &tc_cmd);
}

void ignore_ws(char **c)
{
  while (**c && isspace(**c))
    (*c)++;
}

tt_u16 readhex(char **c)
{
  tt_u16 val = 0;
  char nc;

  ignore_ws(c);

  while (isxdigit(nc = **c)){
    (*c)++;
    val *= 16;
    nc = toupper(nc);
    if (isdigit(nc)) {
      val += nc - '0';
    } else {
      val += nc - 'A' + 10;
    }
  }

  return val;
}

int readint(char **c)
{
  int val = 0;
  char nc;

  ignore_ws(c);

  while (isdigit(nc = **c)){
    (*c)++;
    val *= 10;
    val += nc - '0';
  }

  return val;
}

char *readstr(char **c)
{
  static char buffer[256];
  int i = 0;

  ignore_ws(c);

  while (!isspace(**c) && **c && i < 255)
    buffer[i++] = *(*c)++;

  buffer[i] = '\0';

  return buffer;
}

int more_params(char **c)
{
  ignore_ws(c);
  return (**c) != 0;
}

char next_char(char **c)
{
  ignore_ws(c);
  return *(*c)++;
}

void console_command()
{
  static char input[80], copy[80];
  char *strptr;
  tt_u16 memadr, start, end;
  long n;
  int i, r;
  int regon = 0;
  int ret;

  reset();
  for(;;) {
    if (autof) {
	autof = 0;
	execute();
    }
    activate_console = 0;
    console_active = 1;
    printf("> ");
    fflush(stdout);

    ret = read(0, input, 80);
    if (ret < 0) {
	if (errno == EINTR && reset_hupf) {
	    console_active = 0;
	    reset_hupf = 0;
	    reset_reboot();
	    execute();
	}
	continue;
    }
    input[ret] = 0;
    if (ret == 0) {
	if (noquit == 0) return;
	else printf("quit disabled\n");
    }
    if (strlen(input) == 1)
      strptr = copy;
    else
      strptr = strcpy(copy, input);

    switch (next_char(&strptr)) {
    case 4:
      printf("quit disabled\n");
      break;
    case 'a':
      printf("begin srec upload, '.' to end\n");
      load_motos1_2(stdin);
      break;
    case 'c' :
      for (n = 0; n < 0x10000; n++)
	set_memb((tt_u16)n, 0);
      printf("Memory cleared\n");
      break;
    case 'd' :
      if (more_params(&strptr)) {
	start = readhex(&strptr);
	if (more_params(&strptr))
	  end = readhex(&strptr);
	else
	  end = start;
      } else
	start = end = memadr;

      for(n = start; n <= end && n < 0x10000; n += dis6809((tt_u16)n, stdout));

      memadr = (tt_u16)n;
      break;
    case 'f' :
      if (more_params(&strptr)) {
	console_active = 0;
	execute_addr(readhex(&strptr));
	if (regon) {
	  m6809_dumpregs();
	  printf("Next PC: ");
	  dis6809(rpc, stdout);
	}
	memadr = rpc;
      } else
	printf("Syntax Error. Type 'h' to show help.\n");
      break;
    case 'g' :
      if (more_params(&strptr))
	rpc = readhex(&strptr);
      console_active = 0;
      execute();
      if (regon) {
	m6809_dumpregs();
	printf("Next PC: ");
	dis6809(rpc, stdout);
      }
      memadr = rpc;
      break;
    case 'h' : case '?' :
      printf("     HELP for the 6809 simulator debugger\n\n");
      printf("   a               : upload s-records\n");
      printf("   c               : clear memory\n");
      printf("   d [start] [end] : disassemble memory from <start> to <end>\n");
      printf("   f adr           : step forward until PC = <adr>\n");
      printf("   g [adr]         : start execution at current address or <adr>\n");
      printf("   h, ?            : show this help page\n");
      printf("   l file          : load s-records from <file>\n");
      printf("   m [start] [end] : dump memory from <start> to <end>\n");
      printf("   n [n]           : next [n] instruction(s)\n");
      printf("   p adr           : set PC to <adr>\n");
      printf("   q               : quit the emulator\n");
      printf("   r               : dump CPU registers\n");
#ifdef PC_HISTORY
      printf("   s               : show PC history\n");
      printf("   t               : flush PC history\n");
#endif
      printf("   u               : toggle dump registers\n");
      printf("   w adr           : set watch point address\n");
      printf("   x               : reboot machine\n");
      printf("   y [0]           : show number of 6809 cycles [or set it to 0]\n");
      break;
    case 'l' :
      if (more_params(&strptr))
	// removeme load_intelhex(readstr(&strptr));
	load_motos1(readstr(&strptr));
      else
	printf("Syntax Error. Type 'h' to show help.\n");
      break;
    case 'm' :
      if (more_params(&strptr)) {
	n = readhex(&strptr);
	if (more_params(&strptr))
	  end = readhex(&strptr);
	else
	  end = n;
      } else
	n = end = memadr;
      while (n <= (long)end) {
	printf("%04hX: ", (unsigned int)n);
	for (i = 1; i <= 8; i++)
	  printf("%02X ", get_memb(n++));
	n -= 8;
	for (i = 1; i <= 8; i++) {
	  tt_u8 v;

	  v = get_memb(n++);
	  if (v >= 0x20 && v <= 0x7e)
	    putchar(v);
	  else
	    putchar('.');
	}
	putchar('\n');
      }
      memadr = n;
      break;
    case 'n' :
      if (more_params(&strptr))
	i = readint(&strptr);
      else
	i = 1;

      while (i-- > 0) {
	activate_console = 1;
	if (!execute()) {
	  printf("Next PC: ");
	  memadr = rpc + dis6809(rpc, stdout);
	  if (regon)
	    m6809_dumpregs();
	} else
	  break;
      }
      break;
    case 'p' :
      if(more_params(&strptr))
	rpc = readhex(&strptr);
      else
	printf("Syntax Error. Type 'h' to show help.\n");
      break;
    case 'q' :
      if (noquit == 0) return;
      else printf("quit disabled\n");
      break;
    case 'r' :
      m6809_dumpregs();
      break;
#ifdef PC_HISTORY
    case 's' :
      r = pchistidx - pchistnbr;
      if (r < 0)
	r += PC_HISTORY_SIZE;
      for (i = 1; i <= pchistnbr; i++) {
	dis6809(pchist[r++], stdout);
	if (r == PC_HISTORY_SIZE)
	  r = 0;
      }
      break;
    case 't' :
      pchistnbr = pchistidx = 0;
      break;
#endif
    case 'u' :
      regon ^= 1;
      printf("Dump registers %s\n", regon ? "on" : "off");
      break;
    case 'y' :
      if (more_params(&strptr))
	if(readint(&strptr) == 0) {
	  cycles = 0;
	  printf("Cycle counter initialized\n");
	} else
	  printf("Syntax Error. Type 'h' to show help.\n");
      else {
	  double sec = (double)cycles / (float)cps;

	  printf("Cycle counter: %ld\nEstimated time at %d hz : %g seconds\n", cycles, cps, sec);
      }
      break;
    case 'w':
	if(more_params(&strptr))
	    watchpoint = readhex(&strptr);
	else
	    printf("Syntax Error, address expected\n");
	break;
    case 'x' :
      console_active = 0;
      reset_reboot();
      execute();
      break;
    default :
      printf("Undefined command. Type 'h' to show help.\n");
      break;
    }
  }
}

static void printusage(void) {
    puts("sim6809: -h -n [file [...]]\n"
	 "  -t #  throttle to # cycles per second, 50k default, 0 = max\n"
	 "  -h    print this help\n"
	 "  -n    start machine in debugging console\n"
	 "  -q    don't allow quitting (must be killed)"
	 );
    exit(1);
}


void parse_cmdline(int argc, char **argv)
{
  struct termios t;
  if (argc == 1) {
    autof = 0;
    return;
  }
  argc--;
  argv++;
  while ((*argv)[0] == '-') {
      switch ((*argv)[1]) {
      case 'n':
	  autof = 0;
	  break;
      case 't':
	  cps = atoi(*argv + 2);
	  break;
      case 'q':
	  noquit = 1;
	  tcgetattr(0, &t);
	  t.c_cc[VEOF] = 0;
	  tcsetattr(0, TCSANOW, &t);
	  break;
      case 'd':
	  break;
      case 'h':
      default:
	  printusage();
      }
      argc--;
      argv++;
  }
  while (argc-- > 0) {
      load_motos1(*argv++);
  }
}

void my_atexit(void) {
    tcsetattr(0, TCSANOW, &tc_orig);
}

int main(int argc, char **argv)
{
  if (!memory_init())
    return 1;
  parse_cmdline(argc, argv);
  console_init();
  atexit(my_atexit);
  m6809_init();
  setup_brkhandler();

  // hardware drivers
  hard_init(argc, argv);

  do_alarm(0);

  console_command();

  // unload drivers
  hard_deinit();

  return 0;
}
