/*	$Id$

	Execute an external program using DOS-4B-00

	This file bases on EXEC.C of FreeCOM v0.81 beta 1.

	Returns: DOS error code of DOS-4B

	$Log$
	Revision 1.7  2006/09/05 01:44:33  blairdude
	Massive patches from Arkady that I'm way too lazy to sort through.  If anything happens, Arkady can answer for it.

	Revision 1.6  2004/10/25 19:37:34  skaus
	fix: LH: Errorlevel of program effects LH's error reporting {Eric Auer}
	
	Revision 1.5  2004/02/01 13:52:17  skaus
	add/upd: CVS $id$ keywords to/of files
	
	Revision 1.4  2003/03/05 17:43:52  skaus
	bugfix: cached NLS data not flushed
	
	Revision 1.3  2002/11/12 18:31:57  skaus
	add: save/restore session (swap context) {Tom Ehlert}
	
	Revision 1.2  2002/04/02 18:09:31  skaus
	add: XMS-Only Swap feature (FEATURE_XMS_SWAP) (Tom Ehlert)
	
	Revision 1.1  2001/04/12 00:33:53  skaus
	chg: new structure
	chg: If DEBUG enabled, no available commands are displayed on startup
	fix: PTCHSIZE also patches min extra size to force to have this amount
	   of memory available on start
	bugfix: CALL doesn't reset options
	add: PTCHSIZE to patch heap size
	add: VSPAWN, /SWAP switch, .SWP resource handling
	bugfix: COMMAND.COM A:\
	bugfix: CALL: if swapOnExec == ERROR, no change of swapOnExec allowed
	add: command MEMORY
	bugfix: runExtension(): destroys command[-2]
	add: clean.bat
	add: localized CRITER strings
	chg: use LNG files for hard-coded strings (hangForEver(), init.c)
		via STRINGS.LIB
	add: DEL.C, COPY.C, CBREAK.C: STRINGS-based prompts
	add: fixstrs.c: prompts & symbolic keys
	add: fixstrs.c: backslash escape sequences
	add: version IDs to DEFAULT.LNG and validation to FIXSTRS.C
	chg: splitted code apart into LIB\*.c and CMD\*.c
	bugfix: IF is now using error system & STRINGS to report errors
	add: CALL: /N
	
 */

#include "../config.h"

#include <assert.h>
#include <stdio.h>
#include <dos.h>
#include <string.h>

#include "../include/command.h"
#include "../include/cswap.h"
#include "../include/nls.h"

#include "algnbyte.h"

struct sfcb {
	char bytes[0x25];
};

static unsigned char parseToFCB(const char **str, struct sfcb far *fcbptr, int option)
{
	IREGS r;

	r.r_ax = 0x29 << 8 | (unsigned char)option;
	r.r_ds = FP_SEG(*str);
	r.r_si = FP_OFF(*str);
	r.r_es = FP_SEG(fcbptr);
	r.r_di = FP_OFF(fcbptr);
	intrpt(0x21, &r);

	*str = (const char *)MK_FP(r.r_ds, r.r_si);
	return r.r_ax & 0xff;
}

struct ExecBlock
{
	word segOfEnv;
	char far *cmdLine;
	struct sfcb far *fcb1;
	struct sfcb far *fcb2;
};

#include "algndflt.h"

int cdecl lowLevelExec(char far * cmd, struct ExecBlock far * bl);

int exec(const char *cmd, char *cmdLine, const unsigned segOfEnv)
{
#ifdef FEATURE_XMS_SWAP
#	define buf dosCMDTAIL
#	define memcpy _fmemcpy
#else
	unsigned char buf[MAX_EXTERNAL_COMMAND_SIZE+2]; /* 128 bytes is max size in PSP, 2 bytes for size and terminator */
#endif
	struct sfcb fcb1, fcb2;
	struct ExecBlock execBlock;
	int retval;
	int cmdLen;
	const char *p;

	assert(cmd);
	assert(cmdLine);

	invalidateNLSbuf();

	/* generate Pascal string from the command line */
	/* we assume passed in c string (ASCIIZ) */
	cmdLen = strlen(cmdLine);
	if (cmdLen > MAX_EXTERNAL_COMMAND_SIZE) {
		/* we assume CMDLINE environment variable already set with full cmdLine */
		/* so we simply truncate passed command line to max size, terminated by \r, no \0 for callee */
		/* for maximum compatibility set size to 127 (0x7f) */
		buf[0] = (unsigned char)(MAX_EXTERNAL_COMMAND_SIZE+1);
		memcpy(&buf[1], cmdLine, MAX_EXTERNAL_COMMAND_SIZE);
		/* terminate with just carriage return \r */
		buf[MAX_EXTERNAL_COMMAND_SIZE+1] = '\x0d';
	} else {
		/* set size of actual command line and copy to buffer */
		buf[0] = (unsigned char)cmdLen;
		memcpy(&buf[1], cmdLine, cmdLen);
		/* ensure terminated with \r\0, if less then 126 characters
		   or \r, if exactly 126 characters */
		memcpy(&buf[1] + cmdLen, "\x0d", (cmdLen < MAX_EXTERNAL_COMMAND_SIZE) ? 2 : 1);
	}

	/* fill FCBs */
	p = cmdLine;
	parseToFCB(&p, &fcb1, 1);
	if (p > cmdLine && strlen(p)) {  /* We moved on (hopefully to arg2) and we have length */
		parseToFCB(&p, &fcb2, 1);
	} else {  /* Initialise with all zeros, except spaces for filename.ext */
		memset(&fcb2, 0, sizeof(fcb2));
		memset(&fcb2.bytes[1], ' ', 11);
	}

	saveSession();

#ifdef FEATURE_XMS_SWAP
	if(XMSisactive() && swapOnExec == TRUE) {
		/* Copy the prepared values into the buffers in CSWAP.ASM module */
		_fmemcpy(dosFCB1, &fcb1, sizeof(fcb1));
		_fmemcpy(dosFCB2, &fcb2, sizeof(fcb1));
		assert(strlen(cmd) < 128);
		_fstrcpy((char far *)dosCMDNAME, cmd);
		dosParamDosExec.envSeg = segOfEnv; 

		retval = XMSexec();
		} else
#endif
	{
		/* fill execute structure */
		execBlock.segOfEnv = segOfEnv;
		execBlock.cmdLine = (char far *)buf;
		execBlock.fcb1 = (struct sfcb far *)&fcb1;
		execBlock.fcb2 = (struct sfcb far *)&fcb2;

		retval = lowLevelExec((char far*)cmd, (struct ExecBlock far*)&execBlock);
	}

	restoreSession();
				
	return retval;
}
