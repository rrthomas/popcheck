/* PopCheck.c                                                                 *
 * Author: Staffan Hämälä (staham@algonet.se)                                 *
 * Date started: 28-Apr-1998                                                  *
 *                                                                            *
 * Description:                                                               *
 *  A small utility to check and manage a user on a pop3 server               *
 * Nice to have when you've received lots of big mails you                    *
 * want to get rid of.                                                        *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify       *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation; either version 2 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software                *
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA    *
 *                                                                            *
 * Should you need to contact me, the author, you can do so by                *
 * e-mail - mail your message to <staham@algonet.se>.                         *
 */

#include "config.h"

#include <assert.h>
#include <stdnoreturn.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include <termios.h>
#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

#include "xalloc.h"


#define STRBUFLEN 200

/* Structure definitions */

struct ListNode
{
  struct ListNode *prev;
  struct ListNode *next;
  char subject[60];
  char from[60];
  int size;
  int num;
  int del;
};


/* Global variables */

static char *pophost, *popuser, *poppass, *ifilename, *ofilename;
static int popport = 110;

static int hSocket = -1;
struct sockaddr_in INetSocketAddr;

static char stringbuf[STRBUFLEN];
static char *TopSubject;
static char *TopFrom;
static struct ListNode lh;

static FILE *file, *iofile;
static char passbuff[40];
static char tmpbuffer[500];

static long MailCount;

#define USAGE_STRING "Usage: %s -s server -u user [-P port] [-p password] [-o filename] [-i filename]\n"


static struct ListNode *
AddNode (struct ListNode *node)
{
  struct ListNode *new = (struct ListNode *) XZALLOC (struct ListNode);
  node->next = new;
  new->prev = node;
  return new;
}


static void
AddAllNodes (unsigned long numof)
{
  struct ListNode *this = &lh;
  this->num = 1;

  for (unsigned long a = 2; a <= numof; a++) {
    this = AddNode (this);
    this->num = a;
  }
}


static int
SendDat (char *string)
{
  if (send (hSocket, string, strlen (string), 0) != -1)
    return (TRUE);
  fprintf (stderr, "Socket message send failure\n");
  return (FALSE);
}

static int
RecvDat (char *databuf, int datlen)
{
  int reclen = recv (hSocket, databuf, datlen - 1, 0);
  databuf[reclen] = 0x00;
  return (reclen);
}

/*
----------------------------------------------------------------------
Scans the buffer for the header lines From and Subject.
Subsequent calls will go on searching at the same position that
the buffer for the last call ended. So, if all of the header lines
don't fit into the buffer, no problem, just get the rest of the
lines into the same buffer and call the function a second..(etc) time.

suptr  - Pointer index for subject string (sutext). Used to see what
         character in the subject string to check next time.

sutr   - Subject true. Used to mark that the subject string (sutext)
         has been found so that we now can start copying the subject
         line into the buffer (TopSubject).

subptr - Pointer index for the output buffer (TopSubject).

frptr  - Pointer index for the from string (frtext). Used to see what
         character in the from string to check next time.

frtr   - From true. Used to mark that the from string (frtext) has
         been found so that we now can start copying the from line
         into the buffer (TopFrom).

frbptr - Pointer index for the output buffer (TopFrom).

prnl   - Previous newline. Used to indicate that a newline has
         been found so that the routine knows that the next position
         in the buffer is a new line. Set to 1 as default to
         be able to check the first line too.

----------------------------------------------------------------------
*/
static void
LocateHeaders (char *buffer, int buflen, int reset)
{
  static char frtext[] = "From:";
  static char sutext[] = "Subject:";
  static int suptr = 0;		/* Subject pointer index */
  static int sutr = 0;		/* Subject true */
  static int subptr = 0;	/* Subject buffer pointer index */
  static int frptr = 0;		/* From pointer index */
  static int frtr = 0;		/* From true */
  static int prnl = 1;		/* Previous newline */
  static int frbptr = 0;	/* From buffer pointer index */

  if (reset) {
    suptr = 0;
    sutr = 0;
    subptr = 0;
    frptr = 0;
    frtr = 0;
    frbptr = 0;
    prnl = 1;
  }

  for (int b = 0; b < buflen; b++) {	/* Real routine starts here */
    if (frtr) {			/* If from string has been found (frtext) */
      if (prnl) {		/* Check for continued header line */
        if (buffer[b] == ' ')
          prnl = 0;
        else
          frtr = 0;
      } else {
        if ((buffer[b] != '\n') && (buffer[b] != '\r')) {
          TopFrom[frbptr++] = buffer[b];
          if (frbptr > 50)
            frtr = 0;		/* Stop if line is longer than 50 chars */
        }
      }
    }

    if (sutr) {			/* If subject string has been found (sutext) */
      if (prnl) {		/* Check for continued header line */
        if (buffer[b] == ' ')
          prnl = 0;
        else
          sutr = 0;
      } else {
        if ((buffer[b] != '\n') && (buffer[b] != '\r')) {
          TopSubject[subptr++] = buffer[b];
          if (subptr > 50)
            sutr = 0;		/* Stop if line is longer than 50 chars */
        }
      }
    }

    if (prnl) {
      if ((!suptr) && (!strncasecmp (&buffer[b], &frtext[frptr], 1))) {
        if (frtext[++frptr] == 0x00) {
          frptr = 0;
          frtr = 1;
          prnl = 0;
        }
      } else if ((!frptr) && (!strncasecmp (&buffer[b], &sutext[suptr], 1))) {
        if (sutext[++suptr] == 0x00) {
          suptr = 0;
          sutr = 1;
          prnl = 0;
        }
      } else {
        prnl = 0;
        frptr = 0;
        suptr = 0;
      }
    }
    if (buffer[b] == '\n')
      prnl = 1;
  }				/* Real routine ends here */
}

static int
SendCmd (const char *cmd, char *parm)
{
  char StrBuf[BUFSIZ];
  int StrLen;
  char *buffer;

  if (!parm)
    assert (asprintf (&buffer, "%s\r\n", cmd) >= 0);
  else if (!strncmp ("TOP", cmd, 3))
    assert (asprintf (&buffer, "%s %s 0\r\n", cmd, parm) >= 0);
  else
    assert (asprintf (&buffer, "%s %s\r\n", cmd, parm) >= 0);

  int ret = SendDat (buffer);
  free (buffer);
  if (!ret)
    return (0);

  if (!(StrLen = RecvDat (StrBuf, BUFSIZ)))
    return (0);

  if (!strncmp (StrBuf, "-ERR", 4)) {
    fprintf (stderr, "Bad %s command response: %s\n", cmd, StrBuf);
    return (-1);
  }

  // How many messages ?
  if (!strncmp (cmd, "STAT", 4))
    return (atoi (&StrBuf[4]));

  if (!strncmp (cmd, "LIST", 4)) {
    for (;;) {
      if (!fwrite (StrBuf, StrLen, 1, file)) {
        perror ("Tempfile");
        return (-1);
      }

      if (!strncmp ("\r\n.\r\n", &StrBuf[StrLen - 5], 5))
        break;

      if (!(StrLen = RecvDat (StrBuf, BUFSIZ)))
        return (0);
    }

    if ((StrLen = ftell (file)) == -1) {
      perror ("Tempfile");
      return (-1);
    }

    char *tmpbuf;
    if (!(tmpbuf = (char *) malloc (StrLen))) {
      fprintf (stderr, "Could not allocate memory for tempfile\n");
      return (-1);
    }

    rewind (file);

    if (!fread (tmpbuf, StrLen, 1, file)) {
      free (tmpbuf);
      perror ("Tempfile");
      return (-1);
    }

    int a;
    for (a = 0; tmpbuf[a] != '\n'; a++);

    for (struct ListNode *node = &lh; a < StrLen && node && tmpbuf[a] != '.'; a++) {
      while (tmpbuf[a++] != ' ');
      node->size = atoi (&tmpbuf[a]);

      for (; tmpbuf[a] != '\n'; a++);
      node = node->next;
    }

    free (tmpbuf);
  } else if (!strncmp (cmd, "TOP", 3)) {
    int reset = 1;
    do {
      LocateHeaders (StrBuf, StrLen, reset);
      reset = 0;
    } while (!((StrLen == 3) && (!strncmp (".\r\n", &StrBuf[StrLen - 3], 3))) &&
             strncmp ("\r\n.\r\n", &StrBuf[StrLen - 5], 5) &&
             (StrLen = RecvDat (StrBuf, BUFSIZ)));
  }

  return (0);
}

static void
SocketDisconnect (void)
{
  SendCmd ("QUIT", NULL);
  shutdown (hSocket, SHUT_RDWR);
  close (hSocket);
  printf ("Disconnected from POP Host\n");
}

static int
SocketConnect (void)
{
  struct hostent *HostAddr = gethostbyname (pophost);

  if (!HostAddr) {
    if (h_errno == TRY_AGAIN)
      fprintf (stderr, "Unable to locate the POP Host, try again later\n");
    else
      fprintf (stderr, "The POP Host is invalid\n");

    return (FALSE);
  }

  INetSocketAddr.sin_family = AF_INET;
  INetSocketAddr.sin_port = htons (popport);
  INetSocketAddr.sin_addr.s_addr = 0;

  memcpy (&INetSocketAddr.sin_addr, HostAddr->h_addr, HostAddr->h_length);

  hSocket = socket (AF_INET, SOCK_STREAM, 0);
  if (hSocket == -1) {
    perror ("Socket allocation failed");
    return (FALSE);
  }

  if (connect (hSocket, (struct sockaddr *) &INetSocketAddr, sizeof (INetSocketAddr)) == -1) {
    perror ("Socket connection failed");
    close (hSocket);
    return (FALSE);
  }

  printf ("Sending logon information\n");

  // Get the greeting message.
  int StrLen;
  while ((StrLen = RecvDat (stringbuf, STRBUFLEN)) && stringbuf[StrLen - 1] != '\n');

  if (SendCmd ("USER", popuser) == -1) {
    SocketDisconnect ();
    return (FALSE);		// Send UserID.
  }

  if (SendCmd ("PASS", poppass) == -1) {
    SocketDisconnect ();
    return (FALSE);		// Send Password.
  }

  printf ("Connected to POP Host\n");

  return (TRUE);
}


// ----

noreturn void
finish (int sig)
{
  SocketDisconnect ();
  endwin ();
  exit (sig != 0);
}

noreturn void
MainProg (void)
{
  char *formatstr;
  static char delstr[] = { ' ', 'D' };
  int c, currentline = 0;
  struct ListNode *tempnode, *currentnode;

  (void) signal (SIGINT, finish);	/* arrange interrupts to terminate */

  (void) initscr ();		/* initialize the curses library */
  keypad (stdscr, TRUE);	/* enable keyboard mapping */
  (void) nonl ();		/* tell curses not to do NL->CR/NL on output */
  (void) cbreak ();		/* take input chars one at a time, no wait for \n */
  (void) noecho ();		/* don't echo input */
  scrollok (stdscr, TRUE);

  assert (asprintf (&formatstr, "%%3d: %%c %%-%2.2d.%2.2ds  %%-%2.2d.%2.2ds   %%6ld",
                    (COLS - 18) / 2, (COLS - 18) / 2, (COLS - 18) / 2,
                    (COLS - 18) / 2) >= 0);

  move (LINES - 1, 0);
  attrset (A_BOLD);
  addstr ("USAGE: Q - Quit without saving  S - Quit and save D - Mark for delete");
  attrset (A_NORMAL);

  currentnode = &lh;

  for (;;) {
    for (; currentline >= LINES - 1; currentline--) {
      tempnode = currentnode;
      for (int a = 0; a < LINES - 2; a++)
        if (tempnode->next)
          tempnode = tempnode->next;

      if (tempnode->next)
        currentnode = currentnode->next;
    }

    for (; currentline < 0; currentline++) {
      if (currentnode->prev)
        currentnode = currentnode->prev;
    }

    if (currentline > MailCount - 1)
      currentline = MailCount - 1;

    tempnode = currentnode;
    for (int a = 0; a < LINES - 1; a++) {
      if (a == currentline)
        attrset (A_REVERSE);
      else
        attrset (A_NORMAL);

      move (a, 0);
      clrtoeol ();

      if (tempnode) {
        printw (formatstr, tempnode->num, delstr[tempnode->del],
                tempnode->from, tempnode->subject, tempnode->size);
        tempnode = tempnode->next;
      }

    }

    c = getch ();

    switch (c) {
    case 'd':
      tempnode = currentnode;

      for (int a = 0; a < currentline; a++)
        tempnode = tempnode->next;

      if (tempnode->del)
        tempnode->del = 0;
      else
        tempnode->del = 1;

      currentline++;
      break;

    case 's':
      for (tempnode = &lh; tempnode; tempnode = tempnode->next) {
        if (tempnode->del) {
          assert (asprintf (&formatstr, "%d", tempnode->num) >= 0);	/* Convert int to string */
          SendCmd ("DELE", formatstr);
          free (formatstr);
        }
      }
      finish (0);
      break;

    case 'q':
      finish (0);
      break;

    case KEY_DOWN:
    case 'n':
      currentline++;
      break;

    case KEY_UP:
    case 'p':
      currentline--;
      break;

    case KEY_NPAGE:
    case ' ':
      currentline += LINES - 2;
      break;

    case KEY_PPAGE:
    case '-':
      currentline -= LINES - 2;
      break;

    default:
      break;
    }
  }

  free (formatstr);

  finish (0);			/* we're done */
}


/*
----------------------------------------------------------------------
* Main routine
----------------------------------------------------------------------
*/

int
main (int argc, char *argv[])
{
  struct ListNode *tempnode;
  struct termios oldTermios, newTermios;

  lh.next = NULL;
  lh.prev = NULL;

  char sw;
  while ((sw = getopt (argc, argv, "s:P:u:p:i:o:")) != (char) EOF)
    switch (sw) {
    case 's':			/* Server switch */
      pophost = optarg;
      break;
    case 'P':			/* Port switch */
      popport = atoi (optarg);
      break;
    case 'u':			/* Pop User switch */
      popuser = optarg;
      break;
    case 'p':			/* Pop Password switch */
      poppass = optarg;
      break;
    case 'o':			/* Optional filename */
      ofilename = optarg;
      break;
    case 'i':			/* Optional filename */
      ifilename = optarg;
      break;
    case '?':			/* Unknown switch */
      fprintf (stderr, USAGE_STRING, PACKAGE);
      exit (1);
    default:
      break;
    }

  if ((!pophost) || (!popuser)) {
    fprintf (stderr, USAGE_STRING, PACKAGE);
    exit (1);
  }

  if (!poppass) {
    /* Get the current state of termios */
    tcgetattr (STDIN_FILENO, &newTermios);
    /* Keep a copy of the current setting of termios */
    oldTermios = newTermios;
    /* Remove the echo flag */
    newTermios.c_lflag &= ~ECHO;
    /* Add the ECHONL flag */
    newTermios.c_lflag |= ECHONL;
    /* activate new termios */
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &newTermios);

    printf ("POP Password: ");
    assert (fgets (passbuff, 40, stdin));

    /* reset to old termios */
    tcsetattr (STDIN_FILENO, TCSANOW, &oldTermios);

    poppass = passbuff;
    long a;
    for (a = 0; passbuff[a] && passbuff[a] != 0x0a; a++);
    passbuff[a] = 0x00;
  }


  if (!(file = tmpfile ())) {
    perror ("Tempfile");
    exit (1);
  }

  if (SocketConnect ()) {
    unsigned long a;
    if ((a = SendCmd ("STAT", NULL)) > 0) {
      AddAllNodes (a);
      if ((SendCmd ("LIST", NULL)) != -1) {
        tempnode = &lh;

        printf ("Getting data for message:\n");

        for (unsigned long b = 1; b <= a; b++) {
          char *tmpbuf;
          assert (asprintf (&tmpbuf, "%ld", b) >= 0);	/* Convert int to string */

          TopFrom = tempnode->from;
          TopSubject = tempnode->subject;

          printf ("\r%ld of %ld", b, a);
          fflush (stdout);

          int ret = SendCmd ("TOP", tmpbuf);
          free (tmpbuf);
          if (ret == -1)
            break;

          tempnode = tempnode->next;
        }

        MailCount = a;

        if (ofilename) {
          printf ("\nDumping data to file '%s'... ", ofilename);

          if ((iofile = fopen (ofilename, "w"))) {
            for (tempnode = &lh; tempnode; tempnode = tempnode->next) {
              fprintf (iofile,
                       "%d:%d %-40.40s %-40.40s\n",
                       tempnode->num, tempnode->size,
                       tempnode->from, tempnode->subject);
            }

            printf ("Done\n");
            fclose (iofile);
          } else
            perror (ofilename);
        } else if (ifilename) {
          if ((iofile = fopen (ifilename, "r"))) {
            int b = 0;
            while (fgets (tmpbuffer, 500, iofile)) {
              if (b) {	/* If the last line wasn't completely read into the buffer */
                if (tmpbuffer[strlen (tmpbuffer) - 1] == '\n')
                  b = 0;
                continue;
              }

              b = tmpbuffer[strlen (tmpbuffer) - 1] != '\n';

              for (a = 0; isdigit (tmpbuffer[a]); a++);
              tmpbuffer[a] = 0x00;

              if (!a)
                continue;

              int tmpnum = atoi (tmpbuffer);
              int tmpsize = atoi (&tmpbuffer[a + 1]);

              if (!tmpnum || !tmpsize)
                continue;

              for (tempnode = &lh; tempnode && tempnode->num != tmpnum; tempnode = tempnode->next);
              if (tempnode) {
                if (tempnode->size == tmpsize)
                  SendCmd ("DELE", tmpbuffer);
                else
                  printf ("Wrong message size, skipping message %d (%d<=>%d)\n",
                          tmpnum, tmpsize, tempnode->size);
              }
            }
            printf ("Done!\n");
          } else
            perror (ifilename);
        } else
          MainProg ();
      } else
        fprintf (stderr, "No messages on POP Host\n");
    }
    SocketDisconnect ();
  }

  exit (0);
}
