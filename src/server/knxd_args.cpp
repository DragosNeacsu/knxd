/*
    knxd EIB/KNX bus access and management daemon
    Copyright (C) 2005-2011 Martin Koegler <mkoegler@auto.tuwien.ac.at>
    Copyright (C) 2014 Michael Markstaller <michael@markstaller.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <argp.h>
#include <stdio.h>
#include <iostream>

#include "inifile.h"
#include "types.h"
#include "layer2common.h"

/** aborts program with a printf like message */
void die (const char *msg, ...);

// The NOQUEUE options are deprecated
#define OPT_BACK_TUNNEL_NOQUEUE 1
#define OPT_BACK_TPUARTS_ACKGROUP 2
#define OPT_BACK_TPUARTS_ACKINDIVIDUAL 3
#define OPT_BACK_TPUARTS_DISCH_RESET 4
#define OPT_BACK_EMI_NOQUEUE 5
#define OPT_STOP_NOW 6
#define OPT_FORCE_BROADCAST 7
#define OPT_BACK_SEND_DELAY 8
#define OPT_SINGLE_PORT 9
#define OPT_MULTI_PORT 10
#define OPT_NO_TIMESTAMP 11

#define OPT_ARG(_arg,_state,_default) (arg ? arg : \
        (state->argv[state->next] && state->argv[state->next][0] && (state->argv[state->next][0] != '-')) ?  \
            state->argv[state->next++] : _default)

#define ADD(a,b) do { \
    if (a.size()) a.push_back(','); \
    a.append(b); \
} while(0)

IniData ini;
char link[] = "@";

/** structure to store the arguments */
class arguments
{
public:
  /** port to listen */
  int port;
  /** path for unix domain socket */
  const char *name;
  /** path to pid file */
  const char *pidfile;
  /** path to trace log file */
  const char *daemon;
  /** trace level */
  int tracelevel = -1;
  int errorlevel = -1;
  bool no_timestamps = false;
  /** EIB address (for some backends) */
  eibaddr_t addr;

  /** do I have enough to do? */
  unsigned int has_work;

  /** Start of address block to be assigned dynamically to clients */
  eibaddr_t alloc_addrs;
  /** Length of address block to be assigned dynamically to clients */
  int alloc_addrs_len;
  /* EIBnet/IP multicast server flags */
  bool tunnel;
  bool route;
  bool discover;
  bool single_port = true;
  const char *intf = nullptr;

  L2options l2opts;
  const char *serverip;
  std::string servername = "knxd";

  bool stop_now;
  bool force_broadcast;

public:
  /** The current tracer */
  Array < const char * > filters;

  arguments () { }
  ~arguments () { }

  /** get the current tracer.
   * Call with 'true' when you want to change the tracer
   * and with 'false' when you want to use it.
   *
   * If the current tracer has been used, it's not modified; instead, it is
   * passed to Layer3 (which will deallocate it when it ends) and copied to
   * a new instance.
   */

  void stack(const std::string section, bool clear = true)
    {
      ITER(i,filters)
        ADD(ini[section]["filter"],(*i));
      if (l2opts.send_delay)
        {
          char buf[10];
          snprintf(buf,sizeof(buf),"%d",l2opts.send_delay);
          ini[section]["send-delay"] = buf;
        }
      if (l2opts.flags & FLAG_B_TPUARTS_ACKGROUP)
        ini[section]["ack-group"] = "true";
      if (l2opts.flags & FLAG_B_TPUARTS_ACKINDIVIDUAL)
        ini[section]["ack-individual"] = "true";
      if (l2opts.flags & FLAG_B_TPUARTS_DISCH_RESET)
        ini[section]["disch-reset"] = "true";
      if (l2opts.flags & FLAG_B_NO_MONITOR)
        ini[section]["monitor"] = "false";
      if (tracelevel >= 0 || errorlevel >= 0 || no_timestamps) {
          char b1[10],b2[50];
          snprintf(b2,sizeof(b2),"debug--%s",section.c_str());
          ini["section"]["debug"] = b2;
          if (tracelevel >= 0)
            {
              snprintf(b1,sizeof(b1),"0x%x",tracelevel);
              ini[b2]["trace-level"] = b1;
            }
          if (errorlevel >= 0)
            {
              snprintf(b1,sizeof(b1),"0x%x",errorlevel);
              ini[b2]["error-level"] = b1;
            }
          if (no_timestamps)
            {
              ini[b2]["timestamps"] = "false";
            }
      }
      if (clear)
        reset();
    }
  void reset()
    {
        filters.clear();
        l2opts = L2options();
        tracelevel = -1;
        errorlevel = -1;
        no_timestamps = false;
    }
};

void driver_argsv(const char *arg, char *ap, ...);
void driver_argsv(const char *arg, char *ap, ...)
{
  va_list apl;
  va_start(apl, ap);

  while(ap)
    {
      char *p2 = strchr(ap,':');
      if (p2)
        *p2++ = '\0';
      char *pa = va_arg(apl, char *);
      if (!pa)
        die ("Too many arguments for %s!", arg);
      if (*pa == '!')
        pa++;
      ini[link][pa] = ap;
      ap = p2;
    }
    char *pa = va_arg(apl, char *);
    if (pa && *pa == '!')
      die("%s requires an argument (%s)", arg,pa+1);
    va_end(apl);
}

void driver_args(const char *arg, char *ap)
{
  if(!strcmp(arg,"ip"))
    driver_argsv(arg,ap, "multicast-address","port","interface", NULL);
  else if(!strcmp(arg,"tpuarttcp"))
    driver_argsv(arg,ap, "!ip-address","!dest-port", NULL);
  else if(!strcmp(arg,"usb"))
    driver_argsv(arg,ap, "bus","device","config","interface", NULL);
  else if(!strcmp(arg,"ipt"))
    driver_argsv(arg,ap, "!ip-address","dest-port","src-port", NULL);
  else if(!strcmp(arg,"iptn"))
    {
      driver_argsv(arg,ap, "!ip-address","dest-port","src-port","nat-ip","data-port", NULL);
      ini[link]["nat"] = "true";
    }
  else if(!strcmp(arg,"ft12") || !strcmp(arg,"ncn5120") || !strcmp(arg,"tpuarts"))
    driver_argsv(arg,ap, "!device","baudrate", NULL);
  else if(!strcmp(arg,"dummy"))
    driver_argsv(arg,ap, NULL);
  else
    die ("I don't know of options for %s",arg);
}

/** storage for the arguments*/
arguments arg;

/** number of file descriptors passed in by systemd */
#ifdef HAVE_SYSTEMD
int num_fds;
#else
const int num_fds = 0;
#endif

/** aborts program with a printf like message */
void
die (const char *msg, ...)
{
  va_list ap;
  int err = errno;

  va_start (ap, msg);
  vprintf (msg, ap);
  if (err)
    printf (": %s\n", strerror(err));
  else
    printf ("\n");
  va_end (ap);

  exit (1);
}

/** parses an EIB individual address */
void
readaddr (const char *addr)
{
  int a, b, c;
  if (sscanf (addr, "%d.%d.%d", &a, &b, &c) != 3)
    die ("Address needs to look like X.X.X");
  ini["main"]["addr"] = addr;
}

void
readaddrblock (const char *addr)
{
  int a, b, c, d;
  if (sscanf (addr, "%d.%d.%d:%d", &a, &b, &c, &d) != 4)
    die ("Address block needs to look like X.X.X:X");
  ini["main"]["client-addrs"] = addr;
}

/** version */
static struct argp_option options[] = {
  {"listen-tcp", 'i', "PORT", OPTION_ARG_OPTIONAL,
   "listen at TCP port PORT (default 6720)"},
  {"listen-local", 'u', "FILE", OPTION_ARG_OPTIONAL,
   "listen at Unix domain socket FILE (default /run/knx)"},
  {"no-timestamp", OPT_NO_TIMESTAMP, 0, 0,
   "don't print timestamps when logging"},
  {"trace", 't', "MASK", 0,
   "set trace flags (bitmask)"},
  {"error", 'f', "LEVEL", 0,
   "set error level (default 3: warnings)"},
  {"eibaddr", 'e', "EIBADDR", 0,
   "set our EIB address to EIBADDR (default 0.0.1)"},
  {"client-addrs", 'E', "ADDRSTART", 0,
   "assign addresses ADDRSTART through ADDRSTART+n to clients"},
  {"pid-file", 'p', "FILE", 0, "write the PID of the process to FILE"},
  {"daemon", 'd', "FILE", OPTION_ARG_OPTIONAL,
   "start the programm as daemon. Output will be written to FILE if given"},
#ifdef HAVE_EIBNETIPSERVER
  {"Tunnelling", 'T', 0, 0,
   "enable EIBnet/IP Tunneling in the EIBnet/IP server"},
  {"Routing", 'R', 0, 0,
   "enable EIBnet/IP Routing in the EIBnet/IP server"},
  {"Discovery", 'D', 0, 0,
   "enable the EIBnet/IP server to answer discovery and description requests (SEARCH, DESCRIPTION)"},
  {"Server", 'S', "ip[:port]", OPTION_ARG_OPTIONAL,
   "starts an EIBnet/IP multicast server"},
  {"Interface", 'I', "intf", 0,
   "Interface to use"},
  {"Name", 'n', "SERVERNAME", 0,
   "name of the EIBnet/IP server (default is 'knxd')"},
  {"single-port", OPT_SINGLE_PORT, 0, 0,
   "Use one common port for multicast. This is an ETS4/ETS5 bug workaround."},
  {"multi-port", OPT_MULTI_PORT, 0, 0,
   "Use two ports for multicast. This lets you run multiple KNX processes."},
#endif
  {"layer2", 'b', "driver:[arg]", 0,
   "a Layer-2 driver to use (knxd supports more than one)"},
  {"filter", 'B', "filter:[arg]", 0,
   "a Layer-2 filter to use in front of the next driver"},
#ifdef HAVE_GROUPCACHE
  {"GroupCache", 'c', "SIZE", OPTION_ARG_OPTIONAL,
   "enable caching of group communication network state"},
#endif
#ifdef HAVE_EIBNETIPTUNNEL
  {"no-tunnel-client-queuing", OPT_BACK_TUNNEL_NOQUEUE, 0, 0,
   "wait 30msec between transmitting packets. Obsolete, please use --send-delay=30"},
#endif
#if defined(HAVE_TPUARTs) || defined(HAVE_TPUARTs_TCP)
  {"tpuarts-ack-all-group", OPT_BACK_TPUARTS_ACKGROUP, 0, 0,
   "tpuarts backend should generate L2 acks for all group telegrams"},
  {"tpuarts-ack-all-individual", OPT_BACK_TPUARTS_ACKINDIVIDUAL, 0, 0,
   "tpuarts backend should generate L2 acks for all individual telegrams"},
  {"tpuarts-disch-reset", OPT_BACK_TPUARTS_DISCH_RESET, 0, 0,
   "tpuarts backend should should use a full interface reset (for Disch TPUART interfaces)"},
#endif
  {"send-delay", OPT_BACK_SEND_DELAY, "DELAY", OPTION_ARG_OPTIONAL,
   "wait after sending a packet"},
  {"no-emi-send-queuing", OPT_BACK_EMI_NOQUEUE, 0, 0,
   "wait for ACK after transmitting packets. Obsolete, please use --send-delay=500"},
  {"no-monitor", 'N', 0, 0,
   "the next Layer2 interface may not enter monitor mode"},
  {"allow-forced-broadcast", OPT_FORCE_BROADCAST, 0, 0,
   "Treat routing counter 7 as per KNX spec (dangerous)"},
  {"stop-right-now", OPT_STOP_NOW, 0, OPTION_HIDDEN,
   "immediately stops the server after a successful start"},
  {0}
};

/** parses and stores an option */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct arguments *arguments = (struct arguments *) state->input;
  switch (key)
    {
    case 'T':
      ini["server"]["tunnel"] = "true";
      break;
    case 'R':
      ini["server"]["router"] = "true";
      break;
    case 'D':
      ini["server"]["discover"] = "true";
      break;
    case OPT_SINGLE_PORT:
      ini["server"]["multi-port"] = "true";
      break;
    case OPT_MULTI_PORT:
      ini["server"]["multi-port"] = "false";
      break;
    case 'I':
      ini["server"]["interface"] = arg;
      break;
    case 'S':
      {
        ADD(ini["main"]["links"], "server");
        ini["server"]["driver"] = "server";

        const char *serverip;
        const char *name = arguments->servername.c_str();
        std::string tracename;

        int port = 0;
        char *a = strdup (OPT_ARG(arg, state, ""));
        char *b = strchr (a, ':');
        if (b)
          {
            *b++ = 0;
            if (atoi (b) > 0)
              ini["server"]["port"] = b;
          }
        if (*a) 
          ini["server"]["multicast-address"] = a;

        if (!name || !*name) {
            name = "knxd";
            tracename = "mcast";
        } else {
            tracename = "mcast:";
            tracename += name;
        }
        ini["server"]["trace-name"] = tracename;
        arguments->stack("server");
        break;
      }

    case 'n':
      if (*arg == '=')
	arg++;
      if(strlen(arg) >= 30)
        die("Server name must be shorter than 30 bytes");
      ini["main"]["name"] = arg;
      break;

    case 'u':
      {
        ++*link;
        ADD(ini["main"]["links"], link);
        ini[link]["driver"] = "unix-socket";
        const char *name = OPT_ARG(arg,state,NULL);
        if (name)
          ini[link]["path"] = name;
        arguments->stack(link);
      }
      break;

    case 'i':
      {
        ++*link;
        ADD(ini["main"]["links"], link);
        ini[link]["driver"] = "tcp-socket";
        const char *port = OPT_ARG(arg,state,"");
        if (*port && atoi(port) > 0)
          ini[link]["port"] = port;

        arguments->stack(link);
      }
      break;

    case 't':
      if (arg)
        {
          char *x;
          unsigned long level = strtoul(arg, &x, 0);
          if (*x)
            die ("Trace level: '%s' is not a number", arg);
          arguments->tracelevel = level;
        }
      else
        arguments->tracelevel = -1;
      break;
    case OPT_NO_TIMESTAMP:
      arguments->no_timestamps = true;
      break;
    case 'f':
      arguments->errorlevel = (arg ? atoi (arg) : 0);
      break;
    case 'e':
      if (arguments->filters.size())
        die("You cannot use filters globally.");
      if (arguments->l2opts.flags || arguments->l2opts.send_delay)
        die("You cannot use flags globally.");
      arguments->stack("main");
      readaddr (arg);
      break;
    case 'E':
      readaddrblock (arg);
      break;
    case 'p':
      ini["main"]["pidfile"] = arg;
      break;
    case 'd':
      {
        const char *arg = OPT_ARG(arg,state,NULL);
        ini["main"]["background"] = "true";
        if (arg)
          ini["main"]["logfile"] = arg;
      }
      break;
    case 'c':
      if (arguments->l2opts.flags || arguments->l2opts.send_delay)
        die("You cannot apply flags to the group cache.");
      ini["main"]["cache"] = ++*link;
      arguments->stack(link);
      break;
    case OPT_FORCE_BROADCAST:
      ini["main"]["force-broadcast"] = "true";
      break;
    case OPT_STOP_NOW:
      ini["main"]["stop-after-setup"] = "true";
      break;
    case OPT_BACK_TUNNEL_NOQUEUE: // obsolete
      fprintf(stderr,"The option '--no-tunnel-client-queuing' is obsolete.\n");
      fprintf(stderr,"Please use '--send-delay=30'.");
      arguments->l2opts.send_delay = 30; // msec
      break;
    case OPT_BACK_EMI_NOQUEUE: // obsolete
      fprintf(stderr,"The option '--no-emi-send-queuing' is obsolete.\n");
      fprintf(stderr,"Please use '--send-delay=500'.");
      arguments->l2opts.send_delay = 500; // msec
      break;
    case OPT_BACK_SEND_DELAY:
      arguments->l2opts.send_delay = atoi(OPT_ARG(arg,state,"30"));
      break;
    case OPT_BACK_TPUARTS_ACKGROUP:
      arguments->l2opts.flags |= FLAG_B_TPUARTS_ACKGROUP;
      break;
    case OPT_BACK_TPUARTS_ACKINDIVIDUAL:
      arguments->l2opts.flags |= FLAG_B_TPUARTS_ACKINDIVIDUAL;
      break;
    case OPT_BACK_TPUARTS_DISCH_RESET:
      arguments->l2opts.flags |= FLAG_B_TPUARTS_DISCH_RESET;
      break;
    case 'N':
      arguments->l2opts.flags |= FLAG_B_NO_MONITOR;
      break;
    case ARGP_KEY_ARG:
    case 'b':
      {
        ++*link;
        ADD(ini["main"]["links"], link);
        char *ap = strchr(arg,':');
        if (ap)
          *ap++ = '\0';
        ini[link]["driver"] = arg;
        arguments->stack(link);
        if (ap)
          driver_args(arg,ap);
        break;
      }
    case 'B':
      {
        arguments->filters.push_back(arg);
        break;
      }
    case ARGP_KEY_FINI:

#ifdef HAVE_SYSTEMD
      {
        ADD(ini["main"]["links"], "systemd");
        ini["systemd"]["driver"] = "systemd";
        arguments->stack("systemd");
      }
#endif
      if (arguments->filters.size())
        die ("You need to use filters in front of the affected backend");
      if (arguments->l2opts.flags || arguments->l2opts.send_delay)
	die ("You provided L2 flags after specifying an L2 interface.");
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/** information for the argument parser*/
static struct argp argp = { options, parse_opt, "URL",
"knxd -- a stack for EIB/KNX\n"
"\n"
"Please read the manpage for detailed options.\n"
};

int
main (int ac, char *ag[])
{
  int index;
  setlinebuf(stdout);

  argp_parse (&argp, ac, ag, ARGP_IN_ORDER, &index, &arg);
  ini.write(std::cout);

  return 0;
}
