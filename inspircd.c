/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *    Inspire is copyright (C) 2002 ChatSpike-Dev.
 *                       E-mail:
 *                <brain@chatspike.net>
 *           	  <Craig@chatspike.net>
 *     
 * Written by Craig Edwards, Craig McLure, and others.
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 */

#include "inspircd.h"
#include "inspircd_io.h"
#include "inspircd_util.h"
#include "inspircd_config.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "users.h"

char ServerName[MAXBUF];
char Network[MAXBUF];
char ServerDesc[MAXBUF];
char AdminName[MAXBUF];
char AdminEmail[MAXBUF];
char AdminNick[MAXBUF];
char motd[MAXBUF];
char rules[MAXBUF];
char list[MAXBUF];
char PrefixQuit[MAXBUF];

struct userrec clientlist[MAXCLIENTS];
struct chanrec chanlist[MAXCHAN];
char bannerBuffer[MAXBUF];

int MAXCOMMAND;

/* command callback, all commands define one of these (they can
 * be inside external module files */

typedef void (handlerfunc) (char**, int, struct userrec*);

/* a structure that defines a command */

struct command_t {
	char command[MAXBUF]; /* command name */
	handlerfunc *handler_function; /* handler function as in typedef */
	char flags_needed; /* user flags needed to execute the command or 0 */
	int min_params; /* minimum number of parameters command takes */
};

/* list of commands supported by the ircd */

struct command_t cmdlist[255];

/* chop a string down to 512 characters and preserve linefeed (irc max
 * line length) */

void chop(char* str)
{
	if (strlen(str) > 512)
	{
		str[510] = '\r';
		str[511] = '\n';
		str[512] = '\0';
	}
}

/* write formatted text to a socket, in same format as printf */

void Write(int sock,char *text, ...)
{
  char textbuffer[MAXBUF];
  va_list argsPtr;
  char tb[MAXBUF];

  va_start (argsPtr, text);
  if (!sock)
  {
	  return;
  }
  vsnprintf(textbuffer, MAXBUF, text, argsPtr);
  va_end(argsPtr);
  sprintf(tb,"%s\r\n",textbuffer);
  chop(tb);
  write(sock,tb,strlen(tb));
}

/* write a server formatted numeric response to a single socket */

void WriteServ(int sock, char* text, ...)
{
  char textbuffer[MAXBUF],tb[MAXBUF];
  va_list argsPtr;
  va_start (argsPtr, text);

  if (!sock)
  {
	  return;
  }
  vsnprintf(textbuffer, MAXBUF, text, argsPtr);
  va_end(argsPtr);
  sprintf(tb,":%s %s\r\n",ServerName,textbuffer);
  chop(tb);
  write(sock,tb,strlen(tb));
}

/* write text from an originating user to originating user */

void WriteFrom(int sock, struct userrec *user,char* text, ...)
{
  char textbuffer[MAXBUF],tb[MAXBUF];
  va_list argsPtr;
  va_start (argsPtr, text);

  if (!sock)
  {
	  return;
  }
  vsnprintf(textbuffer, MAXBUF, text, argsPtr);
  va_end(argsPtr);
  sprintf(tb,":%s!%s@%s %s\r\n",user->nick,user->ident,user->dhost,textbuffer);
  chop(tb);
  write(sock,tb,strlen(tb));
}

/* write text to an destination user from a source user (e.g. user privmsg) */

void WriteTo(struct userrec *source, struct userrec *dest,char *data, ...)
{
	char textbuffer[MAXBUF],tb[MAXBUF];
	va_list argsPtr;
	va_start (argsPtr, data);
	if ((!dest) || (!source))
	{
		return;
	}
	vsnprintf(textbuffer, MAXBUF, data, argsPtr);
	va_end(argsPtr);
	chop(tb);
	WriteFrom(dest->fd,source,"%s",textbuffer);
}

/* write formatted text from a source user to all users on a channel
 * including the sender (NOT for privmsg, notice etc!) */

void WriteChannel(struct chanrec* Ptr, struct userrec* user, char* text, ...)
{
	char textbuffer[MAXBUF];
	int i = 0;
	va_list argsPtr;
	va_start (argsPtr, text);
	vsnprintf(textbuffer, MAXBUF, text, argsPtr);
	va_end(argsPtr);
	for (i = 0; i <= MAXCLIENTS; i++)
	{
		if (has_channel(&clientlist[i],Ptr) && (&clientlist[i].fd != 0))
		{
			WriteTo(user,&clientlist[i],"%s",textbuffer);
		}
	}
}

/* write formatted text from a source user to all users on a channel except
 * for the sender (for privmsg etc) */

void ChanExceptSender(struct chanrec* Ptr, struct userrec* user, char* text, ...)
{
	char textbuffer[MAXBUF];
	int i = 0;
	va_list argsPtr;
	va_start (argsPtr, text);
	vsnprintf(textbuffer, MAXBUF, text, argsPtr);
	va_end(argsPtr);
	for (i = 0; i <= MAXCLIENTS; i++)
	{
		if (has_channel(&clientlist[i],Ptr) && (&clientlist[i].fd != 0) && (user != &clientlist[i]))
		{
			WriteTo(user,&clientlist[i],"%s",textbuffer);
		}
	}
}

/* return 0 or 1 depending if users u and u2 share one or more common channels
 * (used by QUIT, NICK etc which arent channel specific notices) */

int common_channels(struct userrec *u, struct userrec *u2)
{
	int i = 0;
	int z = 0;

	if ((!u) || (!u2))
	{
		return 0;
	}
	for (i = 0; i <= MAXCHANS; i++)
	{
		for (z = 0; z <= MAXCHANS; z++)
		{
			if ((u->chans[i].channel == u2->chans[z].channel) && (u->chans[i].channel) && (u2->chans[z].channel))
			{
				return 1;
			}
		}
	}
	return 0;
}

/* write a formatted string to all users who share at least one common
 * channel, including the source user e.g. for use in NICK */

void WriteCommon(struct userrec *u, char* text, ...)
{
	int i = 0;
	char textbuffer[MAXBUF];
	va_list argsPtr;
	va_start (argsPtr, text);
	vsnprintf(textbuffer, MAXBUF, text, argsPtr);
	va_end(argsPtr);

	WriteFrom(u->fd,u,"%s",textbuffer);

	for (i = 0; i <= MAXCLIENTS; i++)
	{
		if (common_channels(u,&clientlist[i]))
		{
			WriteFrom(clientlist[i].fd,u,"%s",textbuffer);
		}
	}
}

/* write a formatted string to all users who share at least one common
 * channel, NOT including the source user e.g. for use in QUIT */

void WriteCommonExcept(struct userrec *u, char* text, ...)
{
	int i = 0;
	char textbuffer[MAXBUF];
	va_list argsPtr;
	va_start (argsPtr, text);
	vsnprintf(textbuffer, MAXBUF, text, argsPtr);
	va_end(argsPtr);

	WriteFrom(u->fd,u,"%s",textbuffer);

	for (i = 0; i <= MAXCLIENTS; i++)
	{
		if ((common_channels(u,&clientlist[i])) && (u != &clientlist[i]))
		{
			WriteFrom(clientlist[i].fd,u,"%s",textbuffer);
		}
	}
}

void WriteOpers(char* text, ...)
{
	int i = 0;
	char textbuffer[MAXBUF];
	va_list argsPtr;
	va_start (argsPtr, text);
	vsnprintf(textbuffer, MAXBUF, text, argsPtr);
	va_end(argsPtr);


	for (i = 0; i <= MAXCLIENTS; i++)
	{
		if ((clientlist[i].fd) && (strstr(clientlist[i].modes,"o")))
		{
			WriteServ(clientlist[i].fd,"NOTICE :%s",textbuffer);
		}
	}
}



/* convert a string to lowercase. Note following special circumstances
 * taken from RFC 1459. Many "official" server branches still hold to this
 * rule so i will too;
 *
 *  Because of IRC's scandanavian origin, the characters {}| are
 *  considered to be the lower case equivalents of the characters []\,
 *  respectively. This is a critical issue when determining the
 *  equivalence of two nicknames.
 */

void strlower(char *n)
{
	int i = 0;
	if (!n)
	{
		return;
	}
	for (i = 0; i <= strlen(n); i++)
	{
		n[i] = tolower(n[i]);
		if (n[i] == '[')
			n[i] = '{';
		if (n[i] == ']')
			n[i] = '}';
		if (n[i] == '\\')
			n[i] = '|';
	}
}

/* verify that a user's nickname is valid */

int isnick(char *n)
{
	int i = 0;
	if (strlen(n) > NICKMAX-1)
	{
		n[NICKMAX-1] = '\0';
	}
	for (i = 0; i <= strlen(n)-1; i++)
	{
		/* can't occur ANYWHERE in a nickname! */
		if (strchr("<>,./?:;@'~#=+()*&%$� \"!",n[i]) != NULL)
		{
			return 0;
		}
		/* can't occur as the first char of a nickname... */
		if ((strchr("0123456789",n[i]) != NULL) && (i == 0))
		{
			return 0;
		}
	}
	return 1;
}

/* Find a user record by nickname and return a pointer to it */

struct userrec* Find(char* nick)
{
	struct userrec* Ptr = NULL;
	int i = 0;
	char n1[MAXBUF],n2[MAXBUF];

	strcpy(n2,nick);
	strlower(n2);
	for (i = 0; i <= MAXCLIENTS; i++)
	{
		strcpy(n1,clientlist[i].nick);
		strlower(n1);
		if (!strcmp(n1,n2))
		{
			Ptr = &clientlist[i];
			return Ptr;
		}
	}
	return NULL;
}

/* find a channel record by channel name and return a pointer to it */

struct chanrec* FindChan(char* chan)
{
	struct chanrec* Ptr = NULL;
	int i = 0;
	char n1[MAXBUF],n2[MAXBUF];

	strcpy(n2,chan);
	strlower(n2);
	for (i = 0; i <= MAXCHAN; i++)
	{
		strcpy(n1,chanlist[i].name);
		strlower(n1);
		if (!strcmp(n1,n2))
		{
			Ptr = &chanlist[i];
			return Ptr;
		}
	}
	return NULL;
}

/* returns the status character for a given user on a channel, e.g. @ for op,
 * % for halfop etc. If the user has several modes set, the highest mode
 * the user has must be returned. */

char* cmode(struct userrec *user, struct chanrec *chan)
{
	int i;
	for (i = 0; i <= MAXCHANS; i++)
	{
		if ((user->chans[i].channel == chan) && (chan != NULL))
		{
			if ((user->chans[i].uc_modes & UCMODE_OP) > 0)
			{
				return "@";
			}
			if ((user->chans[i].uc_modes & UCMODE_HOP) > 0)
			{
				return "%";
			}
			if ((user->chans[i].uc_modes & UCMODE_VOICE) > 0)
			{
				return "+";
			}
			return "";
		}
	}
}

char scratch[MAXMODES];

char* chanmodes(struct chanrec *chan)
{
	strcpy(scratch,"");
	if (chan->noexternal)
	{
		strcat(scratch,"n");
	}
	if (chan->topiclock)
	{
		strcat(scratch,"t");
	}
	if (strcmp(chan->key,""))
	{
		strcat(scratch,"k");
	}
	if (chan->limit)
	{
		strcat(scratch,"l");
	}
	if (chan->inviteonly)
	{
		strcat(scratch,"i");
	}
	if (chan->moderated)
	{
		strcat(scratch,"m");
	}
	if (chan->secret)
	{
		strcat(scratch,"s");
	}
	if (chan->c_private)
	{
		strcat(scratch,"p");
	}
	if (strcmp(chan->key,""))
	{
		strcat(scratch," ");
		strcat(scratch,chan->key);
	}
	if (chan->limit)
	{
		char foo[24];
		sprintf(foo," %d",chan->limit);
		strcat(scratch,foo);
	}
	return scratch;
}

/* returns the status value for a given user on a channel, e.g. STATUS_OP for
 * op, STATUS_VOICE for voice etc. If the user has several modes set, the
 * highest mode the user has must be returned. */

int cstatus(struct userrec *user, struct chanrec *chan)
{
	int i;
	for (i = 0; i <= MAXCHANS; i++)
	{
		if ((user->chans[i].channel == chan) && (chan != NULL))
		{
			if ((user->chans[i].uc_modes & UCMODE_OP) > 0)
			{
				return STATUS_OP;
			}
			if ((user->chans[i].uc_modes & UCMODE_HOP) > 0)
			{
				return STATUS_HOP;
			}
			if ((user->chans[i].uc_modes & UCMODE_VOICE) > 0)
			{
				return STATUS_VOICE;
			}
			return STATUS_NORMAL;
		}
	}
}


/* compile a userlist of a channel into a string, each nick seperated by
 * spaces and op, voice etc status shown as @ and + */

char* userlist(struct chanrec *c)
{
	int i = 0;

	strcpy(list,"");
	for (i = MAXCLIENTS; i >= 0; i--)
	{
		if (has_channel(&clientlist[i],c) && (clientlist[i].fd != 0))
		{
			if (strcmp(clientlist[i].nick,""))
			{
				strcat(list,cmode(&clientlist[i],c));
				strcat(list,clientlist[i].nick);
				strcat(list," ");
			}
		}
	}
	return list;
}

/* return a count of the users on a specific channel */

int usercount(struct chanrec *c)
{
	int i = 0;
	int count = 0;

	strcpy(list,"");
	for (i = MAXCLIENTS; i >= 0; i--)
	{
		if (has_channel(&clientlist[i],c) && (clientlist[i].fd != 0))
		{
			if (strcmp(clientlist[i].nick,""))
			{
				count++;
			}
		}
	}
	return count;
}

/* add a channel to a user, creating the record for it if needed and linking
 * it to the user record */

struct chanrec* add_channel(struct userrec *user, char* cname, char* key)
{
	int i = 0;
	struct chanrec* Ptr;
	int created = 0;

	if ((!cname) || (!user))
	{
		return NULL;
	}
	if (strlen(cname) > CHANMAX-1)
	{
		cname[CHANMAX-1] = '\0';
	}

	if ((has_channel(user,FindChan(cname))) && (FindChan(cname)))
	{
		return; // already on the channel!
	}
	
	if (!FindChan(cname))
	{
		/* create a new one */
		for (i = 0; i <= MAXCHAN ; i++)
		{
			if  (chanlist[i].created == 0)
			{
				strcpy(chanlist[i].name, cname);
				chanlist[i].topiclock = 1;
				chanlist[i].noexternal = 1;
				chanlist[i].created = time(NULL);
				strcpy(chanlist[i].topic, "");
				strncpy(chanlist[i].setby, user->nick,NICKMAX);
				chanlist[i].topicset = 0;
				Ptr = &chanlist[i];
				/* set created to 2 to indicate user
				 * is the first in the channel
				 * and should be given ops */
				created = 2;
				break;
			}
		}
	}
	else
	{
		Ptr = FindChan(cname);
		if (Ptr)
		{
			if (strcmp(Ptr->key,""))
			{
				/* channel has a key... */
			}
		}
		created = 1;
	}

	if (!created)
	{
		WriteServ(user->fd,"405 %s %s :Server unable to create any more channels!",user->nick, cname);
		return NULL;
	}
	
	for (i =0; i <= MAXCHANS; i++)
	{
		if (user->chans[i].channel == NULL)
		{
			if (created == 2) 
			{
				/* first user in is given ops */
				user->chans[i].uc_modes = UCMODE_OP;
			}
			else
			{
				user->chans[i].uc_modes = 0;
			}
			user->chans[i].channel = Ptr;
			WriteChannel(Ptr,user,"JOIN :%s",Ptr->name);
			if (Ptr->topicset)
			{
				WriteServ(user->fd,"332 %s %s :%s", user->nick, Ptr->name, Ptr->topic);
				WriteServ(user->fd,"333 %s %s %s %d", user->nick, Ptr->name, Ptr->setby, Ptr->topicset);
			}
			WriteServ(user->fd,"353 %s = %s :%s", user->nick, Ptr->name, userlist(Ptr));
			WriteServ(user->fd,"366 %s %s :End of /NAMES list.", user->nick, Ptr->name);
			WriteServ(user->fd,"324 %s %s +%s",user->nick, Ptr->name,chanmodes(Ptr));
			WriteServ(user->fd,"329 %s %s %d", user->nick, Ptr->name, Ptr->created);
			return Ptr;
		}
	}
	WriteServ(user->fd,"405 %s %s :You are on too many channels",user->nick, cname);
	return NULL;
}

/* remove a channel from a users record, and remove the record from memory
 * if the channel has become empty */

struct chanrec* del_channel(struct userrec *user, char* cname, char* reason)
{
	int i = 0;
	struct chanrec* Ptr;
	int created = 0;

	if ((!cname) || (!user))
	{
		return NULL;
	}

	Ptr = FindChan(cname);
	
	if (!Ptr)
	{
		return NULL;
	}
	
	for (i =0; i <= MAXCHANS; i++)
	{
		/* zap it from the channel list of the user */
		if (user->chans[i].channel == Ptr)
		{
			if (reason)
			{
				WriteChannel(Ptr,user,"PART %s :%s",Ptr->name, reason);
			}
			else
			{
				WriteChannel(Ptr,user,"PART :%s",Ptr->name);
			}
			user->chans[i].uc_modes = 0;
			user->chans[i].channel = NULL;
			break;
		}
	}
	
	/* if there are no users left on the channel */
	if (!usercount(Ptr))
	{
		/* kill the record */
		for (i = 0; i <= MAXCHAN ; i++)
		{
			if  (&chanlist[i] == Ptr)
			{
				memset(&chanlist[i],0,sizeof(chanlist[i]));
				break;
			}
		}
	}
}

/* returns 1 if user u has channel c in their record, 0 if not */

int has_channel(struct userrec *u, struct chanrec *c)
{
	int i = 0;

	if (!u)
	{
		return 0;
	}
	for (i =0; i <= MAXCHANS; i++)
	{
		if (u->chans[i].channel == c)
		{
			return 1;
		}
	}
	return 0;
}

int give_ops(struct userrec *user,char *dest,struct chanrec *chan,int status)
{
	struct userrec *d;
	int i;
	
	if ((!user) || (!dest) || (!chan))
	{
		return 0;
	}
	if (status != STATUS_OP)
	{
		WriteServ(user->fd,"482 %s %s :You're not a channel operator",user->nick, chan->name);
		return 0;
	}
	else
	{
		d = Find(dest);
		if (!d)
		{
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, dest);
		}
		else
		{
			for (i = 0; i <= MAXCHANS; i++)
			{
				if ((d->chans[i].channel == chan) && (chan != NULL))
				{
					if (d->chans[i].uc_modes & UCMODE_OP)
					{
						/* mode already set on user, dont allow multiple */
						return 0;
					}
					d->chans[i].uc_modes = d->chans[i].uc_modes | UCMODE_OP;
				}
			}
		}
	}
	return 1;
}

int give_hops(struct userrec *user,char *dest,struct chanrec *chan,int status)
{
	struct userrec *d;
	int i;
	
	if ((!user) || (!dest) || (!chan))
	{
		return 0;
	}
	if (status != STATUS_OP)
	{
		WriteServ(user->fd,"482 %s %s :You're not a channel operator",user->nick, chan->name);
		return 0;
	}
	else
	{
		d = Find(dest);
		if (!d)
		{
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, dest);
		}
		else
		{
			for (i = 0; i <= MAXCHANS; i++)
			{
				if ((d->chans[i].channel == chan) && (chan != NULL))
				{
					if (d->chans[i].uc_modes & UCMODE_HOP)
					{
						/* mode already set on user, dont allow multiple */
						return 0;
					}
					d->chans[i].uc_modes = d->chans[i].uc_modes | UCMODE_HOP;
				}
			}
		}
	}
	return 1;
}

int give_voice(struct userrec *user,char *dest,struct chanrec *chan,int status)
{
	struct userrec *d;
	int i;
	
	if ((!user) || (!dest) || (!chan))
	{
		return 0;
	}
	if (status < STATUS_HOP)
	{
		WriteServ(user->fd,"482 %s %s :You must be at least a half-operator",user->nick, chan->name);
		return 0;
	}
	else
	{
		d = Find(dest);
		if (!d)
		{
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, dest);
		}
		else
		{
			for (i = 0; i <= MAXCHANS; i++)
			{
				if ((d->chans[i].channel == chan) && (chan != NULL))
				{
					if (d->chans[i].uc_modes & UCMODE_VOICE)
					{
						/* mode already set on user, dont allow multiple */
						return 0;
					}
					d->chans[i].uc_modes = d->chans[i].uc_modes | UCMODE_VOICE;
				}
			}
		}
	}
	return 1;
}

int take_ops(struct userrec *user,char *dest,struct chanrec *chan,int status)
{
	struct userrec *d;
	int i;
	
	if ((!user) || (!dest) || (!chan))
	{
		return 0;
	}
	if (status != STATUS_OP)
	{
		WriteServ(user->fd,"482 %s %s :You're not a channel operator",user->nick, chan->name);
		return 0;
	}
	else
	{
		d = Find(dest);
		if (!d)
		{
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, dest);
		}
		else
		{
			for (i = 0; i <= MAXCHANS; i++)
			{
				if ((d->chans[i].channel == chan) && (chan != NULL))
				{
					if ((d->chans[i].uc_modes & UCMODE_OP) == 0)
					{
						/* mode already set on user, dont allow multiple */
						return 0;
					}
					d->chans[i].uc_modes ^= UCMODE_OP;
				}
			}
		}
	}
	return 1;
}

int take_hops(struct userrec *user,char *dest,struct chanrec *chan,int status)
{
	struct userrec *d;
	int i;
	
	if ((!user) || (!dest) || (!chan))
	{
		return 0;
	}
	if (status != STATUS_OP)
	{
		WriteServ(user->fd,"482 %s %s :You're not a channel operator",user->nick, chan->name);
		return 0;
	}
	else
	{
		d = Find(dest);
		if (!d)
		{
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, dest);
		}
		else
		{
			for (i = 0; i <= MAXCHANS; i++)
			{
				if ((d->chans[i].channel == chan) && (chan != NULL))
				{
					if ((d->chans[i].uc_modes & UCMODE_HOP) == 0)
					{
						/* mode already set on user, dont allow multiple */
						return 0;
					}
					d->chans[i].uc_modes ^= UCMODE_HOP;
				}
			}
		}
	}
	return 1;
}

int take_voice(struct userrec *user,char *dest,struct chanrec *chan,int status)
{
	struct userrec *d;
	int i;
	
	if ((!user) || (!dest) || (!chan))
	{
		return 0;
	}
	if (status < STATUS_HOP)
	{
		WriteServ(user->fd,"482 %s %s :You must be at least a half-operator",user->nick, chan->name);
		return 0;
	}
	else
	{
		d = Find(dest);
		if (!d)
		{
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, dest);
		}
		else
		{
			for (i = 0; i <= MAXCHANS; i++)
			{
				if ((d->chans[i].channel == chan) && (chan != NULL))
				{
					if ((d->chans[i].uc_modes & UCMODE_VOICE) == 0)
					{
						/* mode already set on user, dont allow multiple */
						return 0;
					}
					d->chans[i].uc_modes ^= UCMODE_VOICE;
				}
			}
		}
	}
	return 1;
}

int add_ban(struct userrec *user,char *dest,struct chanrec *chan,int status)
{
	if ((!user) || (!dest) || (!chan))
	{
		return 0;
	}
}

int take_ban(struct userrec *user,char *dest,struct chanrec *chan,int status)
{
	if ((!user) || (!dest) || (!chan))
	{
		return 0;
	}
}

int channel_mode(struct userrec *user,char mode,struct chanrec *chan,int dir,int status)
{
	if ((!user) || (!chan))
	{
		return 0;
	}

	return 1;
}
	
void process_modes(char **parameters,struct userrec* user,struct chanrec *chan,int status, int pcnt)
{
	char modelist[MAXBUF];
	char outlist[MAXBUF];
	char outstr[MAXBUF];
	char outpars[32][MAXBUF];
	int param = 2;
	int pc = 0;
	int ptr = 0;
	int mdir = 1;
	int r = 0;

	strcpy(modelist,parameters[1]); /* mode list, e.g. +oo-o */
					/* parameters[2] onwards are parameters for
					 * modes that require them :) */
	strcpy(outlist,"");

	for (ptr = 0; ptr < strlen(modelist); ptr++)
	{
		r = 0;

		{
			switch (modelist[ptr])
			{
				case '-':
					mdir = 0;
					strcat(outlist,"-");
				break;			

				case '+':
					mdir = 1;
					strcat(outlist,"+");
				break;

				case 'o':
					if ((param >= pcnt)) break;
					if (mdir == 1)
					{
						r = give_ops(user,parameters[param++],chan,status);
					}
					else
					{
						r = take_ops(user,parameters[param++],chan,status);
					}
					if (r)
					{
						strcat(outlist,"o");
						strcpy(outpars[pc++],parameters[param-1]);
					}
				break;
			
				case 'h':
					if ((param >= pcnt)) break;
					if (mdir == 1)
					{
						r = give_hops(user,parameters[param++],chan,status);
					}
					else
					{
						r = take_hops(user,parameters[param++],chan,status);
					}
					if (r)
					{
						strcat(outlist,"h");
						strcpy(outpars[pc++],parameters[param-1]);
					}
				break;
			
				
				case 'v':
					if ((param >= pcnt)) break;
					if (mdir == 1)
					{
						r = give_voice(user,parameters[param++],chan,status);
					}
					else
					{
						r = take_voice(user,parameters[param++],chan,status);
					}
					if (r)
					{
						strcat(outlist,"v");
						strcpy(outpars[pc++],parameters[param-1]);
					}
				break;
				
				case 'b':
					if ((param >= pcnt)) break;
					if (mdir == 1)
					{
						r = add_ban(user,parameters[param++],chan,status);
					}
					else
					{
						r = take_ban(user,parameters[param++],chan,status);
					}
					if (r)
					{
						strcat(outlist,"b");
						strcpy(outpars[pc++],parameters[param-1]);
					}
				break;

				case 'k':
					if ((param >= pcnt)) break;
					if (mdir == 1)
					{
						if (!strcmp(chan->key,""))
						{
							strcat(outlist,"k");
							strcpy(outpars[pc++],parameters[param++]);
							strcpy(chan->key,parameters[param-1]);
						}
					}
					else
					{
						/* only allow -k if correct key given */
						if (strcmp(chan->key,""))
						{
							strcat(outlist,"k");
							strcpy(chan->key,"");
						}
					}
				break;
				
				case 'l':
					if ((param >= pcnt)) break;
					if (mdir == 1)
					{
						if (!chan->limit)
						{
							strcat(outlist,"l");
							strcpy(outpars[pc++],parameters[param]);
							chan->limit = atoi(parameters[param++]);
						}
					}
					else
					{
						if (chan->limit)
						{
							strcat(outlist,"l");
							chan->limit = 0;
						}
					}
				break;
				
				case 'i':
					if (chan->inviteonly != mdir)
					{
						strcat(outlist,"i");
					}
					chan->inviteonly = mdir;
				break;
				
				case 't':
					if (chan->topiclock != mdir)
					{
						strcat(outlist,"t");
					}
					chan->topiclock = mdir;
				break;
				
				case 'n':
					if (chan->noexternal != mdir)
					{
						strcat(outlist,"n");
					}
					chan->noexternal = mdir;
				break;
				
				case 'm':
					if (chan->moderated != mdir)
					{
						strcat(outlist,"m");
					}
					chan->moderated = mdir;
				break;
				
				case 's':
					if (chan->secret != mdir)
					{
						strcat(outlist,"s");
						if (chan->c_private)
						{
							chan->c_private = 0;
							if (mdir)
							{
								strcat(outlist,"-p+");
							}
							else
							{
								strcat(outlist,"+p-");
							}
						}
					}
					chan->secret = mdir;
				break;
				
				case 'p':
					if (chan->c_private != mdir)
					{
						strcat(outlist,"p");
						if (chan->secret)
						{
							chan->secret = 0;
							if (mdir)
							{
								strcat(outlist,"-s+");
							}
							else
							{
								strcat(outlist,"+s-");
							}
						}
					}
					chan->c_private = mdir;
				break;
				
			}
		}
	}

	/* this ensures only the *valid* modes are sent out onto the network */
	while ((outlist[strlen(outlist)-1] == '-') || (outlist[strlen(outlist)-1] == '+'))
	{
		outlist[strlen(outlist)-1] = '\0';
	}
	if (strcmp(outlist,""))
	{
		strcpy(outstr,outlist);
		for (ptr = 0; ptr < pc; ptr++)
		{
			strcat(outstr," ");
			strcat(outstr,outpars[ptr]);
		}
		WriteChannel(chan,user,"MODE %s %s",chan->name,outstr);
	}
}

void handle_mode(char **parameters, int pcnt, struct userrec *user)
{
	struct chanrec* Ptr;
	struct userrec* dest;
	int can_change,i;
	int direction = 1;
	char outpars[MAXBUF];

	dest = Find(parameters[0]);

	if ((dest) && (pcnt == 1))
	{
		WriteServ(user->fd,"221 %s :+%s",user->nick,user->modes);
		return;
	}
	if ((dest) && (pcnt > 1))
	{
		can_change = 0;
		strcpy(outpars,"");
		if (user != dest)
		{
			if (strstr(user->modes,"o"))
			{
				can_change = 1;
			}
		}
		else
		{
			can_change = 1;
		}
		if (!can_change)
		{
			WriteServ(user->fd,"482 %s :Can't change mmode for other users",user->nick);
			return;
		}
		for (i = 0; i < strlen(parameters[1]); i++)
		{
			if (parameters[1][i] == '+')
			{
				direction = 1;
				strcat(outpars,"+");
			}
			else
			if (parameters[1][i] == '-')
			{
				direction = 0;
				strcat(outpars,"-");
			}
			else
			{
				can_change = 0;
				if (strstr(user->modes,"o"))
				{
					can_change = 1;
				}
				else
				{
					if ((parameters[1][i] == 'i') || (parameters[1][i] == 'w') || (parameters[1][i] == 's'))
					{
						can_change = 1;
					}
				}
				if (can_change)
				{
					if (direction == 1)
					{
						if (!strchr(dest->modes,parameters[1][i]))
						{
							dest->modes[strlen(dest->modes)+1]='\0';
							dest->modes[strlen(dest->modes)] = parameters[1][i];
							outpars[strlen(outpars)+1]='\0';
							outpars[strlen(outpars)] = parameters[1][i];
						}
					}
					else
					{
						int q = 0;
						char temp[MAXBUF];
						char moo[MAXBUF];

						outpars[strlen(outpars)+1]='\0';
						outpars[strlen(outpars)] = parameters[1][i];
						
						strcpy(temp,"");
						for (q = 0; q < strlen(user->modes); q++)
						{
							if (user->modes[q] != parameters[1][i])
							{
								moo[0] = user->modes[q];
								moo[1] = '\0';
								strcat(temp,moo);
							}
						}
						strcpy(user->modes,temp);
					}
				}
			}
		}
		if (strlen(outpars))
		{
			WriteTo(user, dest, "MODE %s :%s", dest->nick, outpars);
		}
		return;
	}
	
	Ptr = FindChan(parameters[0]);
	if (Ptr)
	{
		if (pcnt == 1)
		{
			/* just /modes #channel */
			WriteServ(user->fd,"324 %s %s +%s",user->nick, Ptr->name, chanmodes(Ptr));
			WriteServ(user->fd,"329 %s %s %d", user->nick, Ptr->name, Ptr->created);
		}
		else
		{
			if ((cstatus(user,Ptr) < STATUS_HOP) && (Ptr))
			{
				WriteServ(user->fd,"482 %s %s :You must be at least a half-operator",user->nick, Ptr->name);
				return;
			}

			process_modes(parameters,user,Ptr,cstatus(user,Ptr),pcnt);
		}
	}
	else
	{
		WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
	}
}

/* This function pokes and hacks at a parameter list like the following:
 *
 * PART #winbot, #darkgalaxy :m00!
 *
 * to turn it into a series of individual calls like this:
 *
 * PART #winbot :m00!
 * PART #darkgalaxy :m00!
 *
 * The seperate calls are sent to a callback function provided by the caller
 * (the caller will usually call itself recursively). The callback function
 * must be a command handler. Calling this function on a line with no list causes
 * no action to be taken. You must provide a starting and ending parameter number
 * where the range of the list can be found, useful if you have a terminating
 * parameter as above which is actually not part of the list, or parameters
 * before the actual list as well. This code is used by many functions which
 * can function as "one to list" (see the RFC) */

int loop_call(handlerfunc fn, char **parameters, int pcnt, struct userrec *u, int start, int end, int joins)
{
	char plist[MAXBUF];
	char *param;
	char *pars[32];
	char blog[32][MAXBUF];
	char blog2[32][MAXBUF];
	int i = 0, j = 0, q = 0, total = 0, t = 0, t2 = 0, total2 = 0;
	char keystr[MAXBUF];
	char moo[MAXBUF];

	strcpy(moo,"");
	for (i = 0; i <10; i++)
	{
		if (!parameters[i])
		{
			parameters[i] = moo;
		}
	}
	if (joins)
	{
		if ((pcnt > 1) && (parameters[1])) /* we have a key to copy */
		{
			strcpy(keystr,parameters[1]);
		}
	}

	if (!parameters[start])
	{
		return 0;
	}
	if (!strstr(parameters[start],","))
	{
		return 0;
	}
	strcpy(plist,"");
	for (i = start; i <= end; i++)
	{
		if (parameters[i])
		{
			strcat(plist,parameters[i]);
		}
	}
	
	j = 0;
	param = plist;

	t = strlen(plist);
	for (i = 0; i < t; i++)
	{
		if (plist[i] == ',')
		{
			plist[i] = '\0';
			strcpy(blog[j++],param);
			param = plist+i+1;
		}
	}
	strcpy(blog[j++],param);
	total = j;

	if ((joins) && (keystr))
	{
		if (!strstr(keystr,","))
		{
			j = 0;
			param = keystr;
			t2 = strlen(keystr);
			for (i = 0; i < t; i++)
			{
				if (keystr[i] == ',')
				{
					keystr[i] = '\0';
					strcpy(blog2[j++],param);
					param = keystr+i+1;
				}
			}
			strcpy(blog2[j++],param);
			total2 = j;
		}
	}

	for (j = 0; j < total; j++)
	{
		if (blog[j])
		{
			pars[0] = blog[j];
		}
		for (q = end; q < pcnt-1; q++)
		{
			if (parameters[q+1])
			{
				pars[q-end+1] = parameters[q+1];
			}
		}
		if ((joins) && (parameters[1]))
		{
			if (pcnt > 1)
			{
				if (!strstr(parameters[1],","))
				{
					pars[1] = blog2[j];
				}
			}
		}
		/* repeatedly call the function with the hacked parameter list */
		if ((joins) && (parameters[1]) && (pcnt > 1))
		{
			if (!strstr(parameters[1],","))
			{
				fn(pars,2,u);
			}
			else
			{
				pars[1] = parameters[1];
				fn(pars,2,u);
			}
		}
		else
		{
			fn(pars,pcnt-(end-start),u);
		}
	}

	return 1;
}

void handle_join(char **parameters, int pcnt, struct userrec *user)
{
	struct chanrec* Ptr;
	struct userrec* u;
	int i = 0;
	
	u = user;
	if (loop_call(handle_join,parameters,pcnt,user,0,0,1))
		return;
	Ptr = add_channel(user,parameters[0],parameters[1]);
}


void handle_part(char **parameters, int pcnt, struct userrec *user)
{
	struct chanrec* Ptr;
	struct userrec* u;
	u = user;

	if (pcnt > 1)
	{
		if (loop_call(handle_part,parameters,pcnt,user,0,pcnt-2,0))
			return;
		del_channel(user,parameters[0],parameters[1]);
	}
	else
	{
		if (loop_call(handle_part,parameters,pcnt,user,0,pcnt-1,0))
			return;
		del_channel(user,parameters[0],NULL);
	}
}

void handle_die(char **parameters, int pcnt, struct userrec *user)
{
	WriteOpers("*** DIE command from %s!%s@%s, terminating...",user->nick,user->ident,user->host);
	sleep(5);
	Exit(ERROR);
}

void handle_topic(char **parameters, int pcnt, struct userrec *user)
{
	struct chanrec* Ptr;
	struct userrec* u;
	u = user;

	if (pcnt == 1)
	{
		Ptr = FindChan(parameters[0]);
		if (Ptr->topicset)
		{
			WriteServ(user->fd,"332 %s %s :%s", user->nick, Ptr->name, Ptr->topic);
			WriteServ(user->fd,"333 %s %s %s %d", user->nick, Ptr->name, Ptr->setby, Ptr->topicset);
		}
		else
		{
			WriteServ(user->fd,"331 %s %s :No topic is set.", user->nick, Ptr->name);
		}
	}
	else
	{
		if (loop_call(handle_topic,parameters,pcnt,user,0,pcnt-2,0))
			return;
		Ptr = FindChan(parameters[0]);
		if (Ptr)
		{
			strcpy(Ptr->topic,parameters[1]);
			strcpy(Ptr->setby,user->nick);
			Ptr->topicset = time(NULL);
			WriteChannel(Ptr,user,"TOPIC %s :%s",Ptr->name, Ptr->topic);
		}
		else
		{
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
		}
	}
}

/* sends out an error notice to all connected clients (not to be used
 * lightly!) */

void send_error(char *s)
{
	int i = 0;
	for (i = 0; i <= MAXCLIENTS; i++)
	{
		if (clientlist[i].fd)
		{
			WriteServ(clientlist[i].fd,"NOTICE %s :%s",clientlist[i].nick,s);
		}
	}
}

void Error(int status)
{
  send_error("Error! Segmentation fault! save meeeeeeeeeeeeee *splat!*");
  exit(status);
}

int main (int argc, char *argv[])
{
	Start();
	if (!CheckConfig())
	{
		printf("ERROR: Your config file is missing, this IRCd will self destruct in 10 seconds :p\n");
		Exit(ERROR);
	}
	if (InspIRCd() == ERROR)
	{
		printf("ERROR: could not initialise. Shutting down.\n");
		Exit(ERROR);
	}
	Exit(TRUE);
	return 0;
}


void NonBlocking(int s)
{
  int flags;
  flags = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

/* add a client connection to the sockets list */
void AddClient(int socket, char* host, int port)
{
	int i;
	int blocking = 1;
	NonBlocking(socket);
	for (i = 0; i<=MAXCLIENTS; i++)
	{
		if (clientlist[i].fd == 0) {
			memset(&clientlist[i],0,sizeof(&clientlist[i]));
			clientlist[i].fd = socket;
			strncpy(clientlist[i].host, host,256);
			strncpy(clientlist[i].dhost, host,256);
			strncpy(clientlist[i].server, ServerName,256);
			clientlist[i].registered = 0;
			clientlist[i].signon = time(NULL);
			clientlist[i].nping = time(NULL)+240;
			clientlist[i].lastping = 1;
			clientlist[i].port = port;
			WriteServ(socket,"NOTICE Auth :Looking up your hostname...");
			break;
		}
	}
}

void handle_names(char **parameters, int pcnt, struct userrec *user)
{
	struct chanrec* c;

	if (loop_call(handle_names,parameters,pcnt,user,0,pcnt-1,0))
		return;
	c = FindChan(parameters[0]);
	if (c)
	{
		WriteServ(user->fd,"353 %s = %s :%s", user->nick, c->name, userlist(c));
		WriteServ(user->fd,"366 %s %s :End of /NAMES list.", user->nick, c->name);
	}
	else
	{
		WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
	}
}


void handle_privmsg(char **parameters, int pcnt, struct userrec *user)
{
	struct userrec *dest;
	struct chanrec *chan;
	
	if (loop_call(handle_privmsg,parameters,pcnt,user,0,pcnt-2,0))
		return;
	if (parameters[0][0] == '#')
	{
		chan = FindChan(parameters[0]);
		if (chan)
		{
			ChanExceptSender(chan, user, "PRIVMSG %s :%s", chan->name, parameters[1]);
		}
		else
		{
			/* no such nick/channel */
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
		}
		return;
	}
	
	dest = Find(parameters[0]);
	if (dest)
	{
		WriteTo(user, dest, "PRIVMSG %s :%s", dest->nick, parameters[1]);
	}
	else
	{
		/* no such nick/channel */
		WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
	}
}

void handle_notice(char **parameters, int pcnt, struct userrec *user)
{
	struct userrec *dest;
	struct chanrec *chan;

	if (loop_call(handle_notice,parameters,pcnt,user,0,pcnt-2,0))
		return;
	if (parameters[0][0] == '#')
	{
		chan = FindChan(parameters[0]);
		if (chan)
		{
			WriteChannel(chan, user, "NOTICE %s :%s", chan->name, parameters[1]);
		}
		else
		{
			/* no such nick/channel */
			WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
		}
		return;
	}
	
	dest = Find(parameters[0]);
	if (dest)
	{
		WriteTo(user, dest, "NOTICE %s :%s", dest->nick, parameters[1]);
	}
	else
	{
		/* no such nick/channel */
		WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
	}
}

char lst[MAXBUF];

char* chlist(struct userrec *user)
{
	int i = 0;
	char cmp[MAXBUF];

	strcpy(lst,"");
	if (!user)
	{
		return lst;
	}
	for (i = 0; i < MAXCHANS; i++)
	{
		if (user->chans[i].channel != NULL)
		{
			if (user->chans[i].channel->name)
			{
				strcpy(cmp,user->chans[i].channel->name);
				strcat(cmp," ");
				if (!strstr(lst,cmp))
				{
					strcat(lst,cmode(user,user->chans[i].channel));
					strcat(lst,user->chans[i].channel->name);
					strcat(lst," ");
				}
			}
		}
	}
	return lst;
}

void handle_whois(char **parameters, int pcnt, struct userrec *user)
{
	struct userrec *dest;
	char *t;

	if (loop_call(handle_whois,parameters,pcnt,user,0,pcnt-1,0))
		return;
	dest = Find(parameters[0]);
	if (dest)
	{
		WriteServ(user->fd,"311 %s %s %s %s * :%s",user->nick, dest->nick, dest->ident, dest->dhost, dest->fullname);
		if (user == dest)
		{
			WriteServ(user->fd,"378 %s %s :is connecting from *@%s",user->nick, dest->nick, dest->host);
		}
		if (strcmp(chlist(dest),""))
		{
			WriteServ(user->fd,"319 %s %s :%s",user->nick, dest->nick, chlist(dest));
		}
		WriteServ(user->fd,"312 %s %s %s :%s",user->nick, dest->nick, dest->server, ServerDesc);
		//WriteServ(user->fd,"313 %s %s :is an IRC operator",user->nick, dest->nick);
		//WriteServ(user->fd,"310 %s %s :is available for help.",user->nick, dest->nick);
		WriteServ(user->fd,"317 %s %s %d %d :seconds idle, signon time",user->nick, dest->nick, abs((dest->idle_lastmsg)-time(NULL)), dest->signon);
		
		WriteServ(user->fd,"318 %s %s :End of /WHOIS list.",user->nick, dest->nick);
	}
	else
	{
		/* no such nick/channel */
		WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
		WriteServ(user->fd,"318 %s %s :End of /WHOIS list.",user->nick, parameters[0]);
	}
}

void handle_quit(char **parameters, int pcnt, struct userrec *user)
{
	/* theres more to do here, but for now just close the socket */
	if (pcnt == 1)
	{
		if (parameters[0][0] == ':')
		{
			*parameters[0]++;
		}
		Write(user->fd,"ERROR :Closing link (%s@%s) [%s]",user->ident,user->host,parameters[0]);
		WriteCommonExcept(user,"QUIT :%s: %s",PrefixQuit,parameters[0]);
	}
	else
	{
		Write(user->fd,"ERROR :Closing link (%s@%s) [QUIT]",user->ident,user->host);
		WriteCommonExcept(user,"QUIT :Client exited");
	}
	close(user->fd);
	memset(user,0,sizeof(user));
}

void handle_who(char **parameters, int pcnt, struct userrec *user)
{
	struct chanrec* Ptr;
	int i = 0;
	
	/* theres more to do here, but for now just close the socket */
	if (pcnt == 1)
	{
		if (!strcmp(parameters[0],"0"))
		{
			Ptr = FindChan(user->chans[0].channel->name);
			printf(user->chans[0].channel->name);
			for (i = 0; i <= MAXCLIENTS; i++)
			{
				if ((common_channels(user,&clientlist[i])) && (strcmp(clientlist[i].nick,"")))
				{
					WriteServ(user->fd,"352 %s %s %s %s %s %s Hr@ :0 %s",user->nick, Ptr->name, clientlist[i].ident, clientlist[i].dhost, ServerName, clientlist[i].nick, clientlist[i].fullname);
				}
			}
			WriteServ(user->fd,"315 %s %s :End of /WHO list.",user->nick, Ptr->name);
			return;
		}
		if (parameters[0][0] = '#')
		{
			Ptr = FindChan(parameters[0]);
			if (Ptr)
			{
				for (i = 0; i <= MAXCLIENTS; i++)
				{
					if (has_channel(&clientlist[i],Ptr))
					{
						WriteServ(user->fd,"352 %s %s %s %s %s %s Hr@ :0 %s",user->nick, Ptr->name, clientlist[i].ident, clientlist[i].dhost, ServerName, clientlist[i].nick, clientlist[i].fullname);
					}
				}
				WriteServ(user->fd,"315 %s %s :End of /WHO list.",user->nick, Ptr->name);
			}
			else
			{
				WriteServ(user->fd,"401 %s %s :No suck nick/channel",user->nick, parameters[0]);
			}
		}
	}
}

void handle_list(char **parameters, int pcnt, struct userrec *user)
{
	struct chanrec* Ptr;
	int i = 0;
	
	WriteServ(user->fd,"321 %s Channel :Users Name",user->nick);
	for (i = 0; i < MAXCHAN; i++)
	{
		if (chanlist[i].created)
		{
			WriteServ(user->fd,"322 %s %s %d :[+%s] %s",user->nick,chanlist[i].name,usercount(&chanlist[i]),chanmodes(&chanlist[i]),chanlist[i].topic);
		}
	}
	WriteServ(user->fd,"323 %s :End of channel list.",user->nick);
}


void kill_link(struct userrec *user,char* reason)
{
	Write(user->fd,"ERROR :Closing link (%s@%s) [%s]",user->ident,user->host,reason);
	close(user->fd);
	WriteCommon(user,"QUIT :Ping timeout");
	memset(user,0,sizeof(user));
}

void handle_admin(char **parameters, int pcnt, struct userrec *user)
{
	WriteServ(user->fd,"256 %s :Administrative info for %s",user->nick,ServerName);
	WriteServ(user->fd,"257 %s :Name     - %s",user->nick,AdminName);
	WriteServ(user->fd,"258 %s :Nickname - %s",user->nick,AdminNick);
	WriteServ(user->fd,"258 %s :E-Mail   - %s",user->nick,AdminEmail);
}

void ShowMOTD(struct userrec *user)
{
	FILE* f;
	char linebuf[MAXBUF];
	
	f =  fopen(motd,"r");
	if (f)
	{
		WriteServ(user->fd,"375 %s :- %s message of the day",user->nick,Network);
		while (!feof(f))
		{
			fgets(linebuf,sizeof(linebuf),f);
			linebuf[strlen(linebuf)-1]='\0';
			if (!strcmp(linebuf,""))
			{
				/* allow blank lines */
				strcpy(linebuf,"  ");
			}
			if (!feof(f))
			{
				WriteServ(user->fd,"372 %s :- %s",user->nick,linebuf);
			}
		}
		WriteServ(user->fd,"376 %s :End of %s message of the day.",user->nick,ServerName);
		fclose(f);
	}
	else
	{
		WriteServ(user->fd,"422 %s :Message of the day file is missing.",user->nick);
	}
}

void ShowRULES(struct userrec *user)
{
	FILE* f;
	char linebuf[MAXBUF];
	
	f =  fopen(rules,"r");
	if (f)
	{
		WriteServ(user->fd,"NOTICE %s :*** \002%s\002 rules:",user->nick,ServerName);
		while (!feof(f))
		{
			fgets(linebuf,sizeof(linebuf),f);
			linebuf[strlen(linebuf)-1]='\0';
			if (!strcmp(linebuf,""))
			{
				/* allow blank lines */
				strcpy(linebuf,"  ");
			}
			if (!feof(f))
			{
				WriteServ(user->fd,"NOTICE %s :%s",user->nick,linebuf);
			}
		}
		WriteServ(user->fd,"NOTICE %s :*** End of \002%s\002 rules.",user->nick,ServerName);
		fclose(f);
	}
	else
	{
		WriteServ(user->fd,"NOTICE %s :*** Rules file is missing.",user->nick);
	}
}

/* shows the message of the day, and any other on-logon stuff */
void ConnectUser(struct userrec *user)
{
	user->registered = 7;
	user->idle_lastmsg = time(NULL);
	WriteServ(user->fd,"NOTICE Auth :Welcome to \002%s\002!",Network);
	WriteServ(user->fd,"001 %s :Welcome to the %s IRC Network %s!%s@%s",user->nick,Network,user->nick,user->ident,user->host);
	WriteServ(user->fd,"002 %s :Your host is %s, running version %s",user->nick,ServerName,VERSION);
	WriteServ(user->fd,"003 %s :This server was created %s %s",user->nick,__TIME__,__DATE__);
	WriteServ(user->fd,"004 %s :%s %s iowghraAsORVSxNCWqBzvdHtGI lvhopsmntikrRcaqOALQbSeKVfHGCuzN",user->nick,ServerName,VERSION);
	WriteServ(user->fd,"005 %s :MAP KNOCK SAFELIST HCN MAXCHANNELS=20 MAXBANS=60 NICKLEN=30 TOPICLEN=307 KICKLEN=307 MAXTARGETS=20 AWAYLEN=307 :are supported by this server",user->nick);
	WriteServ(user->fd,"005 %s :WALLCHOPS WATCH=128 SILENCE=5 MODES=13 CHANTYPES=# PREFIX=(ohv)@%c+ CHANMODES=ohvbeqa,kfL,l,psmntirRcOAQKVHGCuzN NETWORK=%s :are supported by this server",user->nick,'%',Network);
	ShowMOTD(user);
	WriteOpers("*** Client connecting on port %d: %s!%s@%s",user->port,user->nick,user->ident,user->host);
}

void handle_version(char **parameters, int pcnt, struct userrec *user)
{
	WriteServ(user->fd,"351 %s :%s %s :(C) ChatSpike 2002",user->nick,VERSION,ServerName);
}

void handle_ping(char **parameters, int pcnt, struct userrec *user)
{
	WriteServ(user->fd,"PONG %s :%s",ServerName,parameters[0]);
}

void handle_pong(char **parameters, int pcnt, struct userrec *user)
{
	// set the user as alive so they survive to next ping
	user->lastping = 1;
}

void handle_motd(char **parameters, int pcnt, struct userrec *user)
{
	ShowMOTD(user);
}

void handle_rules(char **parameters, int pcnt, struct userrec *user)
{
	ShowRULES(user);
}

void handle_user(char **parameters, int pcnt, struct userrec *user)
{
	if (user->registered < 3)
	{
		WriteServ(user->fd,"NOTICE Auth :No ident response, ident prefied with ~");
		strcpy(user->ident,"~"); /* we arent checking ident... but these days why bother anyway? */
		strncat(user->ident,parameters[0],64);
		strncpy(user->fullname,parameters[3],128);
		user->registered = (user->registered | 1);
	}
	else
	{
		WriteServ(user->fd,"462 %s :You may not reregister",user->nick);
		return;
	}
	/* parameters 2 and 3 are local and remote hosts, ignored when sent by client connection */
	if (user->registered == 3)
	{
		/* user is registered now, bit 0 = USER command, bit 1 = sent a NICK command */
		ConnectUser(user);
	}
}

void handle_oper(char **parameters, int pcnt, struct userrec *user)
{
	char LoginName[MAXBUF];
	char Password[MAXBUF];
	char OperType[MAXBUF];
	char TypeName[MAXBUF];
	char Hostname[MAXBUF];
	int i,j;

	for (i = 0; i < ConfValueEnum("oper"); i++)
	{
		ConfValue("oper","name",i,LoginName);
		ConfValue("oper","password",i,Password);
		if ((!strcmp(LoginName,parameters[0])) && (!strcmp(Password,parameters[1])))
		{
			/* correct oper credentials */
			ConfValue("oper","type",i,OperType);
			WriteOpers("*** %s (%s@%s) is now an IRC operator of type %s",user->nick,user->ident,user->host,OperType);
			WriteServ(user->fd,"381 %s :You are now an IRC operator of type %s",user->nick,OperType);
			WriteServ(user->fd,"MODE %s :+o",user->nick);
			for (j =0; j < ConfValueEnum("type"); j++)
			{
				ConfValue("type","name",j,TypeName);
				if (!strcmp(TypeName,OperType))
				{
					/* found this oper's opertype */
					ConfValue("type","host",j,Hostname);
					strncpy(user->dhost,Hostname,256);
				}
			}
			if (!strstr(user->modes,"o"))
			{
				strcat(user->modes,"o");
			}
			return;
		}
	}
	/* no such oper */
	WriteServ(user->fd,"491 %s :Invalid oper credentials",user->nick);
}
				
void handle_nick(char **parameters, int pcnt, struct userrec *user)
{
	if (!strcmp(parameters[0],user->nick))
	{
		return;
	}
	else
	{
		if (parameters[0][0] == ':')
		{
			*parameters[0]++;
		}
		if ((Find(parameters[0])) && (Find(parameters[0]) != user))
		{
			WriteServ(user->fd,"433 %s %s :Nickname is already in use.",user->nick,parameters[0]);
			return;
		}
	}
	if (isnick(parameters[0]) == 0)
	{
		WriteServ(user->fd,"432 %s %s :Erroneous Nickname",user->nick,parameters[0]);
		return;
	}
	if (user->registered == 7)
	{
		WriteCommon(user,"NICK %s",parameters[0]);
	}
	strcpy(user->nick, parameters[0]);
	if (user->registered < 3)
		user->registered = (user->registered | 2);
	if (user->registered == 3)
	{
		/* user is registered now, bit 0 = USER command, bit 1 = sent a NICK command */
		ConnectUser(user);
	}
}

int process_parameters(char **command_p,char *parameters)
{
	int i = 0;
	int j = 0;
	int q = 0;
	q = strlen(parameters);
	command_p[j++] = parameters;
	for (i = 0; i <= q; i++)
	{
		if (parameters[i] == ' ')
		{
			command_p[j++] = parameters+i+1;
			parameters[i] = '\0';
			if (command_p[j-1][0] == ':')
			{
				*command_p[j-1]++; /* remove dodgy ":" */
				break;
				/* parameter like this marks end of the sequence */
			}
		}
	}
	return j; /* returns total number of items in the list */
}

void process_command(struct userrec *user, char* cmd)
{
	char *parameters;
	char *command;
	char *command_p[127];
	char p[MAXBUF];
	int i, j, items, cmd_found;

	if (!cmd)
	{
		return;
	}
	if (!strcmp(cmd,""))
	{
		return;
	}
	/* split the full string into a command plus parameters */
	parameters = p;
	strcpy(p," ");
	command = cmd;
	if (strstr(cmd," "))
	{
		for (i = 0; i <= strlen(cmd); i++)
		{
			/* capitalise the command ONLY, leave params intact */
			cmd[i] = toupper(cmd[i]);
			/* are we nearly there yet?! :P */
			if (cmd[i] == ' ')
			{
				command = cmd;
				parameters = cmd+i+1;
				cmd[i] = '\0';
				break;
			}
		}
	}
	else
	{
		for (i = 0; i <= strlen(cmd); i++)
		{
			cmd[i] = toupper(cmd[i]);
		}
	}
	cmd_found = 0;
	i = 0;
	for (i = 0; i<MAXCOMMAND; i++)
	{
		if (strcmp(cmdlist[i].command,""))
		{
			if (!strcmp(command, cmdlist[i].command))
			{
				if (parameters) {
					if (strcmp(parameters,""))
					{
						items = process_parameters(command_p,parameters);
					}
				}
				else
				{
					items = 0;
					command_p[0] = NULL;
				}
				if (user->fd) {
					user->idle_lastmsg = time(NULL);
					/* activity resets the ping pending timer */
					user->nping = time(NULL) + 120;
					if ((items+1) < cmdlist[i].min_params)
					{
						WriteServ(user->fd,"461 %s %s :Not enough parameters",user->nick,command);
						cmd_found = 1;
						break;
					}
					if ((!strchr(user->modes,cmdlist[i].flags_needed)) && (cmdlist[i].flags_needed))
					{
						WriteServ(user->fd,"481 %s :Permission Denied- You do not have the required operator privilages",user->nick,command);
						cmd_found = 1;
						break;
					}
					if ((user->registered == 7) || (!strcmp(command,"USER")) || (!strcmp(command,"NICK")) || (!strcmp(command,"PASS")))
					{
						cmdlist[i].handler_function(command_p,items,user);
					}
					else
					{
						WriteServ(user->fd,"451 %s :You have not registered",command);
						cmd_found=1;
						break;
					}
				}
				cmd_found = 1;
			}
		}
	}
	if ((!cmd_found) && (user->fd))
	{
		WriteServ(user->fd,"421 %s %s :Unknown command",user->nick,command);
	}
}

void SetupCommandTable(void)
{
  int i = 0;
  
  strcpy(cmdlist[i].command,		"USER");
  cmdlist[i].handler_function = 	handle_user;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	4;
  
  strcpy(cmdlist[i].command,		"NICK");
  cmdlist[i].handler_function = 	handle_nick;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;
  
  strcpy(cmdlist[i].command,		"QUIT");
  cmdlist[i].handler_function = 	handle_quit;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;

  strcpy(cmdlist[i].command,		"VERSION");
  cmdlist[i].handler_function = 	handle_version;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	0;

  strcpy(cmdlist[i].command,		"PING");
  cmdlist[i].handler_function = 	handle_ping;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;

  strcpy(cmdlist[i].command,		"PONG");
  cmdlist[i].handler_function = 	handle_pong;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;

  strcpy(cmdlist[i].command,		"ADMIN");
  cmdlist[i].handler_function = 	handle_admin;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	0;

  strcpy(cmdlist[i].command,		"PRIVMSG");
  cmdlist[i].handler_function = 	handle_privmsg;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	2;

  strcpy(cmdlist[i].command,		"WHOIS");
  cmdlist[i].handler_function = 	handle_whois;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;

  strcpy(cmdlist[i].command,		"NOTICE");
  cmdlist[i].handler_function = 	handle_notice;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	2;

  strcpy(cmdlist[i].command,		"JOIN");
  cmdlist[i].handler_function = 	handle_join;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;

  strcpy(cmdlist[i].command,		"NAMES");
  cmdlist[i].handler_function = 	handle_names;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;

  strcpy(cmdlist[i].command,		"PART");
  cmdlist[i].handler_function = 	handle_part;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;

  strcpy(cmdlist[i].command,		"MODE");
  cmdlist[i].handler_function = 	handle_mode;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	1;

  strcpy(cmdlist[i].command,		"TOPIC");
  cmdlist[i].handler_function = 	handle_topic;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	2;

  strcpy(cmdlist[i].command,		"WHO");
  cmdlist[i].handler_function = 	handle_who;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	2;

  strcpy(cmdlist[i].command,		"MOTD");
  cmdlist[i].handler_function = 	handle_motd;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	2;

  strcpy(cmdlist[i].command,		"RULES");
  cmdlist[i].handler_function = 	handle_rules;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	2;

  strcpy(cmdlist[i].command,		"OPER");
  cmdlist[i].handler_function = 	handle_oper;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	2;

  strcpy(cmdlist[i].command,		"LIST");
  cmdlist[i].handler_function = 	handle_list;
  cmdlist[i].flags_needed=		0;
  cmdlist[i++].min_params=        	0;

  strcpy(cmdlist[i].command,		"DIE");
  cmdlist[i].handler_function = 	handle_die;
  cmdlist[i].flags_needed=		'o';
  cmdlist[i++].min_params=        	0;

  MAXCOMMAND = i;
  
}

void process_buffer(struct userrec *user)
{
	char cmd[MAXBUF];
	int i;
	if (!user->inbuf)
	{
		return;
	}
	if (!strcmp(user->inbuf,""))
	{
		return;
	}
	strncpy(cmd,user->inbuf,MAXBUF);
	if (!strcmp(cmd,""))
	{
		return;
	}
	if ((cmd[strlen(cmd)-1] == '\n') || (cmd[strlen(cmd)-1] == '\r'))
	{
		cmd[strlen(cmd)-1] = '\0';
	}
	if ((cmd[strlen(cmd)-1] == '\n') || (cmd[strlen(cmd)-1] == '\r'))
	{
		cmd[strlen(cmd)-1] = '\0';
	}
	strcpy(user->inbuf,"");
	if (!strcmp(cmd,""))
	{
		return;
	}
	process_command(user,cmd);
}

int InspIRCd(void)
{
  struct sockaddr_in client, server;
  int length, portCount = 0, ports[MAXSOCKS];
  int openSockfd[MAXSOCKS], incomingSockfd, result = TRUE;
  int count = 0, scanDetectTrigger = TRUE, showBanner = FALSE, boundPortCount = 0;
  int selectResult = 0;
  char *temp, configToken[MAXBUF], stuff[MAXBUF];
  char resolvedHost[MAXBUF];
  fd_set selectFds;
  struct timeval tv;
  int count2;

  if ((geteuid()) && (getuid()) == 0)
  {
	printf("WARNING!!! You are running an irc server as ROOT!!! DO NOT DO THIS!!!\n\n");
	Exit(ERROR);
  }
  SetupCommandTable();
  memset(clientlist, 0, sizeof(clientlist));
  memset(chanlist,0,sizeof(chanlist));

  ConfValue("server","name",0,ServerName);
  ConfValue("server","description",0,ServerDesc);
  ConfValue("server","network",0,Network);
  ConfValue("admin","name",0,AdminName);
  ConfValue("admin","email",0,AdminEmail);
  ConfValue("admin","nick",0,AdminNick);
  ConfValue("files","motd",0,motd);
  ConfValue("files", "rules",0,rules);
  ConfValue("options","prefixquit",0,PrefixQuit);

  for (count = 0; count < ConfValueEnum("bind"); count++)
  {
	ConfValue("bind","port",count,configToken);
	ports[count] = atoi(configToken);
  }
  portCount = ConfValueEnum("bind");

  printf("InspIRCd is now running!\n");

  if (DaemonSeed() == ERROR)
  {
     printf("ERROR: could not go into daemon mode. Shutting down.\n");
     Exit(ERROR);
  }
  
  
  /* setup select call */
  FD_ZERO(&selectFds);

  for (count = 0; count < portCount; count++)
  {
      if ((openSockfd[boundPortCount] = OpenTCPSocket()) == ERROR)
      {
	  return(ERROR);
      }

      if (BindSocket(openSockfd[boundPortCount],client,server,ports[count]) == ERROR)
      {
      }
      else			/* well we at least bound to one socket so we'll continue */
      {
	  boundPortCount++;
      }
  }

  /* if we didn't bind to anything then abort */
  if (boundPortCount == 0)
  {
     return (ERROR);
  }

  length = sizeof (client);

  /* main loop for multiplexing/resetting */
  for (;;)
  {
      /* set up select call */
      for (count = 0; count < boundPortCount; count++)
      {
		FD_SET (openSockfd[count], &selectFds);
      }
      /* added timeout! select was waiting forever... wank... :/ */
      tv.tv_sec = 0;
      tv.tv_usec = 5;
      selectResult = select(MAXSOCKS, &selectFds, NULL, NULL, &tv);

	for (count2 = 0; count2<MAXCLIENTS; count2++)
	{
		char data[MAXBUF];
		if ((clientlist[count2].fd))
		{
			if (((time(NULL))>clientlist[count2].nping) && (strcmp(clientlist[count2].nick,"")) && (clientlist[count2].registered == 7))
			{
				if (!clientlist[count2].lastping) 
				{
					kill_link(&clientlist[count2],"Ping timeout");
				}
				Write(clientlist[count2].fd,"PING :%s",ServerName);
				clientlist[count2].lastping = 0;
				clientlist[count2].nping = time(NULL)+120;
			}
			
			
			result = read(clientlist[count2].fd, data, 1);
			// result == 0 means nothing read
			if (result == EAGAIN)
			{
			}
			if (result == ENOTSOCK)
			{
				kill_link(&clientlist[count2],"Dead socket");
			}
			if (result == ENETUNREACH)
			{
				kill_link(&clientlist[count2],"Network unreachable");
			}
			if (result == ETIMEDOUT)
			{
				kill_link(&clientlist[count2],"Connection timed out");
			}
			if (result == ECONNRESET)
			{
				kill_link(&clientlist[count2],"Connection reset by peer");
			}
			else if (result < -1)
			{
			}
			else if (result > 0)
			{
				if ((data) && (clientlist[count2].inbuf) && (clientlist[count2].fd))
				{
					strncat(clientlist[count2].inbuf, data, result);
					if (strstr(clientlist[count2].inbuf, "\n") || strstr(clientlist[count2].inbuf, "\t"))
					{
						/* at least one complete line is waiting to be processed */
						if (clientlist[count2].fd)
						{
							process_buffer(&clientlist[count2]);
						}
						if (clientlist[count2].fd == 0)
						{
							break;
						}
					}
				}
			}
		}
	}
      
      /* something blew up */
      if (selectResult < 0)
      {
      }
      if (selectResult == 0)
      {
      }
      /* select is reporting a waiting socket. Poll them all to find out which */
      else if (selectResult > 0)
      {
	for (count = 0; count < boundPortCount; count++)		
        {
	    if (FD_ISSET (openSockfd[count], &selectFds))
            {
	      char target[MAXBUF];
              incomingSockfd = accept (openSockfd[count], (struct sockaddr *) &client, &length);
	      SafeStrncpy (target, (char *) inet_ntoa (client.sin_addr), MAXBUF);
 	      if (incomingSockfd < 0)
	      {
	        WriteOpers("*** WARNING: Accept failed on port %d (%s)", ports[count],target);
	        break;
	      }
	      AddClient(incomingSockfd, target,ports[count]);
	      break;
	    }

	}
      }
  }

  /* not reached */
  close (incomingSockfd);
}

