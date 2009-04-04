/* guestfish - the filesystem interactive shell
 * Copyright (C) 2009 Red Hat Inc. 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include <guestfs.h>

#include "fish.h"

struct mp {
  struct mp *next;
  char *device;
  char *mountpoint;
};

static void mount_mps (struct mp *mp);
static void interactive (void);
static void shell_script (void);
static void script (int prompt);
static void cmdline (char *argv[], int optind, int argc);
static int issue_command (const char *cmd, char *argv[]);

/* Currently open libguestfs handle. */
guestfs_h *g;
int g_launched = 0;

int quit = 0;
int verbose = 0;

int
launch (void)
{
  if (!g_launched) {
    if (guestfs_launch (g) == -1)
      return -1;
    if (guestfs_wait_ready (g) == -1)
      return -1;
    g_launched = 1;
  }
  return 0;
}

static void
usage (void)
{
  fprintf (stderr,
	   "guestfish: guest filesystem shell\n"
	   "guestfish lets you edit virtual machine filesystems\n"
	   "Copyright (C) 2009 Red Hat Inc.\n"
	   "Usage:\n"
	   "  guestfish [--options] cmd [: cmd : cmd ...]\n"
	   "or for interactive use:\n"
	   "  guestfish\n"
	   "or from a shell script:\n"
	   "  guestfish <<EOF\n"
	   "  cmd\n"
	   "  ...\n"
	   "  EOF\n"
	   "Options:\n"
	   "  -h|--cmd-help       List available commands\n"
	   "  -h|--cmd-help cmd   Display detailed help on 'cmd'\n"
	   "  -a image            Add image\n"
	   "  -m dev[:mnt]        Mount dev on mnt (if omitted, /)\n"
	   "  -n|--no-sync        Don't autosync\n"
	   /*"  --ro|-r             All mounts are read-only\n"*/
	   "  -v|--verbose        Verbose messages\n"
	   "For more information, see the manpage guestfish(1).\n");
}

int
main (int argc, char *argv[])
{
  static const char *options = "a:h::m:v?";
  static struct option long_options[] = {
    { "add", 1, 0, 'a' },
    { "cmd-help", 2, 0, 'h' },
    { "help", 0, 0, '?' },
    { "mount", 1, 0, 'm' },
    { "no-sync", 0, 0, 'n' },
    { "verbose", 0, 0, 'v' },
    { 0, 0, 0, 0 }
  };
  struct mp *mps = NULL;
  struct mp *mp;
  char *p;
  int c;

  /* guestfs_create is meant to be a lightweight operation, so
   * it's OK to do it early here.
   */
  g = guestfs_create ();
  if (g == NULL) {
    fprintf (stderr, "guestfs_create: failed to create handle\n");
    exit (1);
  }

  guestfs_set_autosync (g, 1);

  for (;;) {
    c = getopt_long (argc, argv, options, long_options, NULL);
    if (c == -1) break;

    switch (c) {
    case 'a':
      if (access (optarg, R_OK) != 0) {
	perror (optarg);
	exit (1);
      }
      if (guestfs_add_drive (g, optarg) == -1)
	exit (1);
      break;

    case 'h':
      if (optarg)
	display_command (optarg);
      else if (argv[optind] && argv[optind][0] != '-')
	display_command (argv[optind++]);
      else
	list_commands ();
      exit (0);

    case 'm':
      mp = malloc (sizeof (struct mp));
      if (!mp) {
	perror ("malloc");
	exit (1);
      }
      p = strchr (optarg, ':');
      if (p) {
	*p = '\0';
	mp->mountpoint = p+1;
      } else
	mp->mountpoint = "/";
      mp->device = optarg;
      mp->next = mps;
      mps = mp->next;
      break;

    case 'n':
      guestfs_set_autosync (g, 0);
      break;

    case 'v':
      verbose++;
      guestfs_set_verbose (g, verbose);
      break;

    case '?':
      usage ();
      exit (0);

    default:
      fprintf (stderr, "guestfish: unexpected command line option 0x%x\n", c);
      exit (1);
    }
  }

  /* If we've got mountpoints, we must launch the guest and mount them. */
  if (mps != NULL) {
    if (launch () == -1) exit (1);
    mount_mps (mps);
  }

  /* Interactive, shell script, or command(s) on the command line? */
  if (optind >= argc) {
    if (isatty (0))
      interactive ();
    else
      shell_script ();
  }
  else
    cmdline (argv, optind, argc);

  exit (0);
}

void
pod2text (const char *heading, const char *str)
{
  FILE *fp;

  fp = popen ("pod2text", "w");
  if (fp == NULL) {
    /* pod2text failed, maybe not found, so let's just print the
     * source instead, since that's better than doing nothing.
     */
    printf ("%s\n\n%s\n", heading, str);
    return;
  }
  fputs ("=head1 ", fp);
  fputs (heading, fp);
  fputs ("\n\n", fp);
  fputs (str, fp);
  pclose (fp);
}

/* List is built in reverse order, so mount them in reverse order. */
static void
mount_mps (struct mp *mp)
{
  if (mp) {
    mount_mps (mp->next);
    if (guestfs_mount (g, mp->device, mp->mountpoint) == -1)
      exit (1);
  }
}

static void
interactive (void)
{
  script (1);
}

static void
shell_script (void)
{
  script (0);
}

static void
script (int prompt)
{
  char buf[8192];
  char *cmd;
  char *argv[64];
  int len, i;

  if (prompt)
    printf ("\n"
	    "Welcome to guestfish, the libguestfs filesystem interactive shell for\n"
	    "editing virtual machine filesystems.\n"
	    "\n"
	    "Type: 'help' for help with commands\n"
	    "      'quit' to quit the shell\n"
	    "\n");

  while (!quit) {
    if (prompt) printf ("><fs> ");
    if (fgets (buf, sizeof buf, stdin) == NULL) {
      quit = 1;
      break;
    }

    len = strlen (buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

    /* Split the buffer up at whitespace. */
    cmd = strtok (buf, " \t");
    if (cmd == NULL)
      continue;

    i = 0;
    while (i < sizeof argv / sizeof argv[0] &&
	   (argv[i] = strtok (NULL, " \t")) != NULL)
      i++;
    if (i == sizeof argv / sizeof argv[0]) {
      fprintf (stderr, "guestfish: too many arguments in command\n");
      exit (1);
    }

    (void) issue_command (cmd, argv);
  }
  if (prompt) printf ("\n");
}

static void
cmdline (char *argv[], int optind, int argc)
{
  const char *cmd;
  char **params;

  if (optind >= argc) return;

  cmd = argv[optind++];
  if (strcmp (cmd, ":") == 0) {
    fprintf (stderr, "guestfish: empty command on command line\n");
    exit (1);
  }
  params = &argv[optind];

  /* Search for end of command list or ":" ... */
  while (optind < argc && strcmp (argv[optind], ":") != 0)
    optind++;

  if (optind == argc) {
    if (issue_command (cmd, params) == -1) exit (1);
  } else {
    argv[optind] = NULL;
    if (issue_command (cmd, params) == -1) exit (1);
    cmdline (argv, optind+1, argc);
  }
}

static int
issue_command (const char *cmd, char *argv[])
{
  int argc;

  for (argc = 0; argv[argc] != NULL; ++argc)
    ;

  if (strcasecmp (cmd, "help") == 0) {
    if (argc == 0)
      list_commands ();
    else
      display_command (argv[0]);
    return 0;
  }
  else if (strcasecmp (cmd, "quit") == 0 ||
	   strcasecmp (cmd, "exit") == 0 ||
	   strcasecmp (cmd, "q") == 0)
    exit (0);
  else if (strcasecmp (cmd, "add") == 0 ||
	   strcasecmp (cmd, "drive") == 0 ||
	   strcasecmp (cmd, "add_drive") == 0) {
    if (argc != 1) {
      fprintf (stderr, "use 'add image' to add a guest image\n");
      return -1;
    }
    else
      return guestfs_add_drive (g, argv[0]);
  }
  else if (strcasecmp (cmd, "cdrom") == 0) {
    if (argc != 1) {
      fprintf (stderr, "use 'cdrom image' to add a CD-ROM image\n");
      return -1;
    }
    else
      return guestfs_add_cdrom (g, argv[0]);
  }
  else if (strcasecmp (cmd, "launch") == 0) {
    if (argc != 0) {
      fprintf (stderr, "'launch' command takes no parameters\n");
      return -1;
    }
    else
      return launch ();
  }
  else
    return run_action (cmd, argc, argv);
}

void
list_builtin_commands (void)
{
  printf ("%-20s %s\n",
	  "help", "display a list of commands or help on a command");
  printf ("%-20s %s\n",
	  "quit", "quit guestfish");
  printf ("%-20s %s\n",
	  "add", "add a guest image to be examined or modified");
  printf ("%-20s %s\n",
	  "cdrom", "add a CD-ROM image to be examined");
  printf ("%-20s %s\n",
	  "launch", "launch the subprocess");
}

void
display_builtin_command (const char *cmd)
{
  if (strcasecmp (cmd, "add") == 0)
    printf ("add - add a guest image to be examined or modified\n"
	    "     add <image>\n");
  else if (strcasecmp (cmd, "cdrom") == 0)
    printf ("cdrom - add a CD-ROM image to be examined\n"
	    "     cdrom <iso-file>\n");
  else if (strcasecmp (cmd, "help") == 0)
    printf ("help - display a list of commands or help on a command\n"
	    "     help cmd\n"
	    "     help\n");
  else if (strcasecmp (cmd, "quit") == 0)
    printf ("quit - quit guestfish\n"
	    "     quit\n");
  else if (strcasecmp (cmd, "launch") == 0)
    printf ("launch - launch the subprocess\n"
	    "     launch\n");
  else
    fprintf (stderr, "%s: command not known, use -h to list all commands\n",
	     cmd);
}
